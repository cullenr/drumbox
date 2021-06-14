/*
  ==============================================================================

    OOPSInstrument.c
    Created: 20 Jan 2017 12:01:54pm
    Author:  Michael R Mulshine

  ==============================================================================
*/

#if _WIN32 || _WIN64

#include "..\Inc\OOPSInstrument.h"
#include "..\Inc\OOPS.h"

#else

#include "../Inc/OOPSInstrument.h"
#include "../Inc/OOPS.h"

#endif



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 *
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#if N_TALKBOX

tTalkbox* tTalkboxInit()
{
    tTalkbox* v = &oops.tTalkboxRegistry[oops.registryIndex[T_TALKBOX]++];

    v->param[0] = 0.5f;  //wet
    v->param[1] = 0.0f;  //dry
    v->param[2] = 0; // Swap
    v->param[3] = 1.0f;  //quality
    
    tTalkboxUpdate(v);
    
    return v;
}

void tTalkboxUpdate(tTalkbox* const v) ///update internal parameters...
{
    float fs = oops.sampleRate;
    if(fs <  8000.0f) fs =  8000.0f;
    if(fs > 96000.0f) fs = 96000.0f;
    
    int32_t n = (int32_t)(0.01633f * fs);
    if(n > TALKBOX_BUFFER_LENGTH) n = TALKBOX_BUFFER_LENGTH;
    
    //O = (VstInt32)(0.0005f * fs);
    v->O = (int32_t)((0.0001f + 0.0004f * v->param[3]) * fs);
    
    if(n != v->N) //recalc hanning window
    {
        v->N = n;
        float dp = TWO_PI / v->N;
        float p = 0.0f;
        for(n=0; n<v->N; n++)
        {
            v->window[n] = 0.5f - 0.5f * cosf(p);
            p += dp;
        }
    }
    v->wet = 0.5f * v->param[0] * v->param[0];
    v->dry = 2.0f * v->param[1] * v->param[1];
}


void tTalkboxSuspend(tTalkbox* const v) ///clear any buffers...
{
    v->pos = v->K = 0;
    v->emphasis = 0.0f;
    v->FX = 0;
    
    v->u0 = v->u1 = v->u2 = v->u3 = v->u4 = 0.0f;
    v->d0 = v->d1 = v->d2 = v->d3 = v->d4 = 0.0f;
    
    for (int32_t i = 0; i < TALKBOX_BUFFER_LENGTH; i++)
    {
        v->buf0[i] = 0;
        v->buf1[i] = 0;
        v->car0[i] = 0;
        v->car1[i] = 0;
    }
}


#define ORD_MAX           100 // Was 50. Increasing this gets rid of glitchiness, lowering it breaks it; not sure how it affects performance
void tTalkboxLpc(float *buf, float *car, int32_t n, int32_t o)
{
    float z[ORD_MAX], r[ORD_MAX], k[ORD_MAX], G, x;
    int32_t i, j, nn=n;
    
    for(j=0; j<=o; j++, nn--)  //buf[] is already emphasized and windowed
    {
        z[j] = r[j] = 0.0f;
        for(i=0; i<nn; i++) r[j] += buf[i] * buf[i+j]; //autocorrelation
    }
    r[0] *= 1.001f;  //stability fix
    
    float min = 0.00001f;
    if(r[0] < min) { for(i=0; i<n; i++) buf[i] = 0.0f; return; }
    
    tTalkboxLpcDurbin(r, o, k, &G);  //calc reflection coeffs
    
    for(i=0; i<=o; i++)
    {
        if(k[i] > 0.995f) k[i] = 0.995f; else if(k[i] < -0.995f) k[i] = -.995f;
    }
    
    for(i=0; i<n; i++)
    {
        x = G * car[i];
        for(j=o; j>0; j--)  //lattice filter
        {
            x -= k[j] * z[j-1];
            z[j] = z[j-1] + k[j] * x;
        }
        buf[i] = z[0] = x;  //output buf[] will be windowed elsewhere
    }
}


void tTalkboxLpcDurbin(float *r, int p, float *k, float *g)
{
    int i, j;
    float a[ORD_MAX], at[ORD_MAX], e=r[0];
    
    for(i=0; i<=p; i++) a[i] = at[i] = 0.0f; //probably don't need to clear at[] or k[]
    
    for(i=1; i<=p; i++)
    {
        k[i] = -r[i];
        
        for(j=1; j<i; j++)
        {
            at[j] = a[j];
            k[i] -= a[j] * r[i-j];
        }
        if(fabs(e) < 1.0e-20f) { e = 0.0f;  break; }
        k[i] /= e; // This might be costing us
        
        a[i] = k[i];
        for(j=1; j<i; j++) a[j] = at[j] + k[i] * at[i-j];
        
        e *= 1.0f - k[i] * k[i];
    }
    
    if(e < 1.0e-20f) e = 0.0f;
    *g = sqrtf(e);
}

float tTalkboxTick(tTalkbox* const v, float synth, float voice)
{

    int32_t  p0=v->pos, p1 = (v->pos + v->N/2) % v->N;
    float e=v->emphasis, w, o, x, dr, fx=v->FX;
    float p, q, h0=0.3f, h1=0.77f;
    
    o = voice;
    x = synth;
    
    dr = o;
    
    p = v->d0 + h0 *  x; v->d0 = v->d1;  v->d1 = x  - h0 * p;
    q = v->d2 + h1 * v->d4; v->d2 = v->d3;  v->d3 = v->d4 - h1 * q;
    v->d4 = x;
    x = p + q;
    
    if(v->K++)
    {
        v->K = 0;
        
        v->car0[p0] = v->car1[p1] = x; //carrier input
        
        x = o - e;  e = o;  //6dB/oct pre-emphasis
        
        w = v->window[p0]; fx = v->buf0[p0] * w;  v->buf0[p0] = x * w;  //50% overlapping hanning windows
        if(++p0 >= v->N) { tTalkboxLpc(v->buf0, v->car0, v->N, v->O);  p0 = 0; }
        
        w = 1.0f - w;  fx += v->buf1[p1] * w;  v->buf1[p1] = x * w;
        if(++p1 >= v->N) { tTalkboxLpc(v->buf1, v->car1, v->N, v->O);  p1 = 0; }
    }
    
    p = v->u0 + h0 * fx; v->u0 = v->u1;  v->u1 = fx - h0 * p;
    q = v->u2 + h1 * v->u4; v->u2 = v->u3;  v->u3 = v->u4 - h1 * q;
    v->u4 = fx;
    x = p + q;
    
    o = x;
    
    v->emphasis = e;
    v->pos = p0;
    v->FX = fx;

    float den = 1.0e-10f; //(float)pow(10.0f, -10.0f * param[4]);
    if(fabs(v->d0) < den) v->d0 = 0.0f; //anti-denormal (doesn't seem necessary but P4?)
    if(fabs(v->d1) < den) v->d1 = 0.0f;
    if(fabs(v->d2) < den) v->d2 = 0.0f;
    if(fabs(v->d3) < den) v->d3 = 0.0f;
    if(fabs(v->u0) < den) v->u0 = 0.0f;
    if(fabs(v->u1) < den) v->u1 = 0.0f;
    if(fabs(v->u2) < den) v->u2 = 0.0f;
    if(fabs(v->u3) < den) v->u3 = 0.0f;
    return o;
}

#endif

#if N_VOCODER

tVocoder*   tVocoderInit        (void)
{
    tVocoder* v = &oops.tVocoderRegistry[oops.registryIndex[T_VOCODER]++];
    
    v->param[0] = 0.33f;  //input select
    v->param[1] = 0.50f;  //output dB
    v->param[2] = 0.40f;  //hi thru
    v->param[3] = 0.40f;  //hi band
    v->param[4] = 0.16f;  //envelope
    v->param[5] = 0.55f;  //filter q
    v->param[6] = 0.6667f;//freq range
    v->param[7] = 0.33f;  //num bands
    
    tVocoderUpdate(v);
    
    return v;
}

void        tVocoderUpdate      (tVocoder* const v)
{
    float tpofs = 6.2831853f * oops.invSampleRate;
    
    float rr, th, re;
    
    float sh;
    
    int32_t i;
    
    v->swap = 1; if(v->param[0]>0.5f) v->swap = 0;
    
    v->gain = (float)pow(10.0f, 2.0f * v->param[1] - 3.0f * v->param[5] - 2.0f);
    
    v->thru = (float)pow(10.0f, 0.5f + 2.0f * v->param[1]);
    v->high =  v->param[3] * v->param[3] * v->param[3] * v->thru;
    v->thru *= v->param[2] * v->param[2] * v->param[2];
    
    if(v->param[7]<0.5f)
    {
        v->nbnd=8;
        re=0.003f;
        v->f[1][2] = 3000.0f;
        v->f[2][2] = 2200.0f;
        v->f[3][2] = 1500.0f;
        v->f[4][2] = 1080.0f;
        v->f[5][2] = 700.0f;
        v->f[6][2] = 390.0f;
        v->f[7][2] = 190.0f;
    }
    else
    {
        v->nbnd=16;
        re=0.0015f;
        v->f[ 1][2] = 5000.0f; //+1000
        v->f[ 2][2] = 4000.0f; //+750
        v->f[ 3][2] = 3250.0f; //+500
        v->f[ 4][2] = 2750.0f; //+450
        v->f[ 5][2] = 2300.0f; //+300
        v->f[ 6][2] = 2000.0f; //+250
        v->f[ 7][2] = 1750.0f; //+250
        v->f[ 8][2] = 1500.0f; //+250
        v->f[ 9][2] = 1250.0f; //+250
        v->f[10][2] = 1000.0f; //+250
        v->f[11][2] =  750.0f; //+210
        v->f[12][2] =  540.0f; //+190
        v->f[13][2] =  350.0f; //+155
        v->f[14][2] =  195.0f; //+100
        v->f[15][2] =   95.0f;
    }
    
    if(v->param[4]<0.05f) //freeze
    {
        for(i=0;i<v->nbnd;i++) v->f[i][12]=0.0f;
    }
    else
    {
        v->f[0][12] = (float)pow(10.0, -1.7 - 2.7f * v->param[4]); //envelope speed
        
        rr = 0.022f / (float)v->nbnd; //minimum proportional to frequency to stop distortion
        for(i=1;i<v->nbnd;i++)
        {
            v->f[i][12] = (float)(0.025 - rr * (double)i);
            if(v->f[0][12] < v->f[i][12]) v->f[i][12] = v->f[0][12];
        }
        v->f[0][12] = 0.5f * v->f[0][12]; //only top band is at full rate
    }
    
    rr = 1.0 - pow(10.0f, -1.0f - 1.2f * v->param[5]);
    sh = (float)pow(2.0f, 3.0f * v->param[6] - 1.0f); //filter bank range shift
    
    for(i=1;i<v->nbnd;i++)
    {
        v->f[i][2] *= sh;
        th = acos((2.0 * rr * cos(tpofs * v->f[i][2])) / (1.0 + rr * rr));
        v->f[i][0] = (float)(2.0 * rr * cos(th)); //a0
        v->f[i][1] = (float)(-rr * rr);           //a1
        //was .98
        v->f[i][2] *= 0.96f; //shift 2nd stage slightly to stop high resonance peaks
        th = acos((2.0 * rr * cos(tpofs * v->f[i][2])) / (1.0 + rr * rr));
        v->f[i][2] = (float)(2.0 * rr * cos(th));
    }
}

float       tVocoderTick        (tVocoder* const v, float synth, float voice)
{
    float a, b, o=0.0f, aa, bb, oo = v->kout, g = v->gain, ht = v->thru, hh = v->high, tmp;
    uint32_t i, k = v->kval, sw = v->swap, nb = v->nbnd;

    a = voice; //speech
    b = synth; //synth
    
    if(sw==0) { tmp=a; a=b; b=tmp; } //swap channels
    
    tmp = a - v->f[0][7]; //integrate modulator for HF band and filter bank pre-emphasis
    v->f[0][7] = a;
    a = tmp;
    
    if(tmp<0.0f) tmp = -tmp;
    v->f[0][11] -= v->f[0][12] * (v->f[0][11] - tmp);      //high band envelope
    o = v->f[0][11] * (ht * a + hh * (b - v->f[0][3])); //high band + high thru
    
    v->f[0][3] = b; //integrate carrier for HF band
    
    if(++k & 0x1) //this block runs at half sample rate
    {
        oo = 0.0f;
        aa = a + v->f[0][9] - v->f[0][8] - v->f[0][8];  //apply zeros here instead of in each reson
        v->f[0][9] = v->f[0][8];  v->f[0][8] = a;
        bb = b + v->f[0][5] - v->f[0][4] - v->f[0][4];
        v->f[0][5] = v->f[0][4];  v->f[0][4] = b;
        
        for(i=1; i<nb; i++) //filter bank: 4th-order band pass
        {
            tmp = v->f[i][0] * v->f[i][3] + v->f[i][1] * v->f[i][4] + bb;
            v->f[i][4] = v->f[i][3];
            v->f[i][3] = tmp;
            tmp += v->f[i][2] * v->f[i][5] + v->f[i][1] * v->f[i][6];
            v->f[i][6] = v->f[i][5];
            v->f[i][5] = tmp;
            
            tmp = v->f[i][0] * v->f[i][7] + v->f[i][1] * v->f[i][8] + aa;
            v->f[i][8] = v->f[i][7];
            v->f[i][7] = tmp;
            tmp += v->f[i][2] * v->f[i][9] + v->f[i][1] * v->f[i][10];
            v->f[i][10] = v->f[i][9];
            v->f[i][9] = tmp;
            
            if(tmp<0.0f) tmp = -tmp;
            v->f[i][11] -= v->f[i][12] * (v->f[i][11] - tmp);
            oo += v->f[i][5] * v->f[i][11];
        }
    }
    o += oo * g; //effect of interpolating back up to Fs would be minimal (aliasing >16kHz)

    v->kout = oo;
    v->kval = k & 0x1;
    if(fabs(v->f[0][11])<1.0e-10) v->f[0][11] = 0.0f; //catch HF envelope denormal
    
    for(i=1;i<nb;i++)
        if(fabs(v->f[i][3])<1.0e-10 || fabs(v->f[i][7])<1.0e-10)
            for(k=3; k<12; k++) v->f[i][k] = 0.0f; //catch reson & envelope denormals
    
    if(fabs(o)>10.0f) tVocoderSuspend(v); //catch instability
    
    return o;
    
}

void        tVocoderSuspend     (tVocoder* const v)
{
    int32_t i, j;
    
    for(i=0; i<v->nbnd; i++) for(j=3; j<12; j++) v->f[i][j] = 0.0f; //zero band filters and envelopes
    v->kout = 0.0f;
    v->kval = 0;
}

#endif

 
#if N_PLUCK
/* ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ tPluck ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ */
tPluck*    tPluckInit         (float lowestFrequency,  float delayBuff[DELAY_LENGTH])
{
    tPluck* p = &oops.tPluckRegistry[oops.registryIndex[T_PLUCK]++];
    
    if ( lowestFrequency <= 0.0f )  lowestFrequency = 10.0f;
    
    p->noise = tNoiseInit(WhiteNoise);
    p->pickFilter = tOnePoleInit(0.0f);
    p->loopFilter = tOneZeroInit(0.0f);
    
    
    p->delayLine = tDelayAInit(0.0f);
    
    tPluckSetFrequency(p, 220.0f);
    
    p->sampleRateChanged = &tPluckSampleRateChanged;
    
    return p;
    
}

float   tPluckGetLastOut    (tPluck *p)
{
    return p->lastOut;
}

float   tPluckTick          (tPluck *p)
{
    return (p->lastOut = 3.0f * tDelayATick(p->delayLine, tOneZeroTick(p->loopFilter, tDelayAGetLastOut(p->delayLine) * p->loopGain ) ));
}

void    tPluckPluck         (tPluck* const p, float amplitude)
{
    if ( amplitude < 0.0f)      amplitude = 0.0f;
    else if (amplitude > 1.0f)  amplitude = 1.0f;
    
    tOnePoleSetPole(p->pickFilter, 0.999f - (amplitude * 0.15f));
    tOnePoleSetGain(p->pickFilter, amplitude * 0.5f );
    
    // Fill delay with noise additively with current contents.
    for ( uint32_t i = 0; i < (uint32_t)tDelayAGetDelay(p->delayLine); i++ )
        tDelayATick(p->delayLine, 0.6f * tDelayAGetLastOut(p->delayLine) + tOnePoleTick(p->pickFilter, tNoiseTick(p->noise) ) );
}

// Start a note with the given frequency and amplitude.;
void    tPluckNoteOn        (tPluck* const p, float frequency, float amplitude )
{
    p->lastFreq = frequency;
    tPluckSetFrequency( p, frequency );
    tPluckPluck( p, amplitude );
}

// Stop a note with the given amplitude (speed of decay).
void    tPluckNoteOff       (tPluck* const p, float amplitude )
{
    if ( amplitude < 0.0f)      amplitude = 0.0f;
    else if (amplitude > 1.0f)  amplitude = 1.0f;
    
    p->loopGain = 1.0f - amplitude;
}

// Set instrument parameters for a particular frequency.
void    tPluckSetFrequency  (tPluck* const p, float frequency )
{
    if ( frequency <= 0.0f )   frequency = 0.001f;
    
    // Delay = length - filter delay.
    float delay = ( oops.sampleRate / frequency ) - tOneZeroGetPhaseDelay(p->loopFilter, frequency );
    
    tDelayASetDelay(p->delayLine, delay );
    
    p->loopGain = 0.99f + (frequency * 0.000005f);
    
    if ( p->loopGain >= 0.999f ) p->loopGain = 0.999f;
    
}

// Perform the control change specified by \e number and \e value (0.0 - 128.0).
void    tPluckControlChange (tPluck* const p, int number, float value)
{
    return;
}

void tPluckSampleRateChanged(tPluck* const p)
{
    //tPluckSetFrequency(p, p->lastFreq);
}

#endif
/* ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ */

#if N_STIFKARP
/* ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ tStifKarp ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ */
tStifKarp*    tStifKarpInit          (float lowestFrequency, float delayBuff[2][DELAY_LENGTH])
{
    tStifKarp* p = &oops.tStifKarpRegistry[oops.registryIndex[T_STIFKARP]++];
    
    if ( lowestFrequency <= 0.0f )  lowestFrequency = 8.0f;
    
    p->delayLine = tDelayAInit(0.0f);
    p->combDelay = tDelayLInit(0.0f);
    
    p->filter = tOneZeroInit(0.0f);
    
    p->noise = tNoiseInit(WhiteNoise);
    
    for (int i = 0; i < 4; i++)
        p->biquad[i] = tBiQuadInit();
    
    p->pluckAmplitude = 0.3f;
    p->pickupPosition = 0.4f;
    
    p->stretching = 0.9999f;
    p->baseLoopGain = 0.995f;
    p->loopGain = 0.999f;
    
    tStifKarpSetFrequency( p, 220.0f );
    
    p->sampleRateChanged = &tStifKarpSampleRateChanged;
    
    return p;
    
}

float   tStifKarpGetLastOut    (tStifKarp* const p)
{
    return p->lastOut;
}

float   tStifKarpTick          (tStifKarp* const p)
{
    float temp = tDelayAGetLastOut(p->delayLine) * p->loopGain;
    
    // Calculate allpass stretching.
    for (int i=0; i<4; i++)     temp = tBiQuadTick(p->biquad[i],temp);
    
    // Moving average filter.
    temp = tOneZeroTick(p->filter, temp);
    
    float out = tDelayATick(p->delayLine, temp);
    out = out - tDelayLTick(p->combDelay, out);
    p->lastOut = out;
    
    return p->lastOut;
}

void    tStifKarpPluck         (tStifKarp* const p, float amplitude)
{
    if ( amplitude < 0.0f)      amplitude = 0.0f;
    else if (amplitude > 1.0f)  amplitude = 1.0f;
    
    p->pluckAmplitude = amplitude;
    
    for ( uint32_t i=0; i < (uint32_t)tDelayAGetDelay(p->delayLine); i++ )
    {
        // Fill delay with noise additively with current contents.
        tDelayATick(p->delayLine, (tDelayAGetLastOut(p->delayLine) * 0.6f) + 0.4f * tNoiseTick(p->noise) * p->pluckAmplitude );
        //delayLine_.tick( combDelay_.tick((delayLine_.lastOut() * 0.6) + 0.4 * noise->tick() * pluckAmplitude_) );
    }
}

// Start a note with the given frequency and amplitude.;
void    tStifKarpNoteOn        (tStifKarp* const p, float frequency, float amplitude )
{
    tStifKarpSetFrequency( p, frequency );
    tStifKarpPluck( p, amplitude );
}

// Stop a note with the given amplitude (speed of decay).
void    tStifKarpNoteOff       (tStifKarp* const p, float amplitude )
{
    if ( amplitude < 0.0f)      amplitude = 0.0f;
    else if (amplitude > 1.0f)  amplitude = 1.0f;
    
    p->loopGain = 1.0f - amplitude;
}

// Set instrument parameters for a particular frequency.
void    tStifKarpSetFrequency  (tStifKarp* const p, float frequency )
{
    if ( frequency <= 0.0f )   frequency = 0.001f;
    
    p->lastFrequency = frequency;
    p->lastLength = oops.sampleRate / p->lastFrequency;
    float delay = p->lastLength - 0.5f;
    tDelayASetDelay(p->delayLine, delay );
    
    // MAYBE MODIFY LOOP GAINS
    p->loopGain = p->baseLoopGain + (frequency * 0.000005f);
    if (p->loopGain >= 1.0f) p->loopGain = 0.99999f;
    
    tStifKarpSetStretch(p, p->stretching);
    
    tDelayLSetDelay(p->combDelay, 0.5f * p->pickupPosition * p->lastLength );
    
}

// Set the stretch "factor" of the string (0.0 - 1.0).
void    tStifKarpSetStretch         (tStifKarp* const p, float stretch )
{
    p->stretching = stretch;
    float coefficient;
    float freq = p->lastFrequency * 2.0f;
    float dFreq = ( (0.5f * oops.sampleRate) - freq ) * 0.25f;
    float temp = 0.5f + (stretch * 0.5f);
    if ( temp > 0.9999f ) temp = 0.9999f;
    
    for ( int i=0; i<4; i++ )
    {
        coefficient = temp * temp;
        tBiQuadSetA2(p->biquad[i], coefficient);
        tBiQuadSetB0(p->biquad[i], coefficient);
        tBiQuadSetB2(p->biquad[i], 1.0f);
        
        coefficient = -2.0f * temp * cos(TWO_PI * freq / oops.sampleRate);
        tBiQuadSetA1(p->biquad[i], coefficient);
        tBiQuadSetB1(p->biquad[i], coefficient);
        
        freq += dFreq;
    }
}

// Set the pluck or "excitation" position along the string (0.0 - 1.0).
void    tStifKarpSetPickupPosition  (tStifKarp* const p, float position )
{
    if (position < 0.0f)        p->pickupPosition = 0.0f;
    else if (position <= 1.0f)  p->pickupPosition = position;
    else                        p->pickupPosition = 1.0f;
    
    tDelayLSetDelay(p->combDelay, 0.5f * p->pickupPosition * p->lastLength);
}

// Set the base loop gain.
void    tStifKarpSetBaseLoopGain    (tStifKarp* const p, float aGain )
{
    p->baseLoopGain = aGain;
    p->loopGain = p->baseLoopGain + (p->lastFrequency * 0.000005f);
    if ( p->loopGain > 0.99999f ) p->loopGain = 0.99999f;
}

// Perform the control change specified by \e number and \e value (0.0 - 128.0).
void    tStifKarpControlChange (tStifKarp* const p, SKControlType type, float value)
{
    if ( value < 0.0f )         value = 0.0f;
    else if (value > 128.0f)   value = 128.0f;
    
    float normalizedValue = value * INV_128;
    
    if (type == SKPickPosition) // 4
        tStifKarpSetPickupPosition( p, normalizedValue );
    else if (type == SKStringDamping) // 11
        tStifKarpSetBaseLoopGain( p, 0.97f + (normalizedValue * 0.03f) );
    else if (type == SKDetune) // 1
        tStifKarpSetStretch( p, 0.91f + (0.09f * (1.0f - normalizedValue)) );
    
}

void    tStifKarpSampleRateChanged (tStifKarp* const c)
{
    tStifKarpSetFrequency(c, c->lastFrequency);
    tStifKarpSetStretch(c, c->stretching);
}

#endif //N_STIFKARP
