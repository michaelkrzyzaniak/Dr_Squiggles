/*
 *  Synth.h
 *  Make weird noises
 *
 *  Made by Michael Krzyzaniak at Arizona State University's
 *  School of Arts, Media + Engineering in Spring of 2013
 *  mkrzyzan@asu.edu
 */

#include "Microphone.h"
#include "../Robot_Communication_Framework/Robot_Communication.h"
#include "Click.h"
#include "Timestamp.h"

#include <pthread.h>

#define MIC_RHYTHM_THREAD_RUN_LOOP_INTERVAL 1000 //usec

void  mic_onset_detected_callback(void* SELF, unsigned long long sample_time);
void  mic_beat_detected_callback (void* SELF, unsigned long long sample_time);
void  mic_message_recd_from_robot(void* self, char* message, robot_arg_t args[], int num_args);

void* mic_rhythm_thread_run_loop (void* SELF);

/*--------------------------------------------------------------------*/
struct OpaqueMicrophoneStruct
{
  AUDIO_GUTS               ;
  BTT* btt                 ;
  Click*  click            ;
  Robot*  robot            ;
  int     silent_beat_count;
  int     max_silent_beats ;
  
  Rhythm* rhythm;
  int     rhythm_constructor_index;
  
  float*  rhythm_onsets;
  int     rhythm_onsets_len;
  int     num_rhythm_onsets;
  int     rhythm_onsets_index;
  
  pthread_t rhythm_thread;
  int     rhythm_thread_run_loop_running;
  
  pthread_mutex_t rhythm_generator_swap_mutex;

  unsigned beat_clock;
  unsigned long long thread_clock;
  
};

/*--------------------------------------------------------------------*/
int         mic_audio_callback  (void* SELF, auSample_t* buffer, int num_frames, int num_channels);
Microphone* mic_destroy         (Microphone* self);

/*--------------------------------------------------------------------*/
Microphone* mic_new()
{
  Microphone* self = (Microphone*) auAlloc(sizeof(*self), mic_audio_callback, NO, 2);
  
  if(self != NULL)
    {
      self->destroy = (Audio* (*)(Audio*))mic_destroy;
    
      self->click = click_new();
      if(self->click == NULL)
        return (Microphone*)auDestroy((Audio*)self);
    
      self->robot = robot_new(mic_message_recd_from_robot, self);
      if(self->robot == NULL)
        return (Microphone*)auDestroy((Audio*)self);
    
      self->btt = btt_new_default();
      if(self->btt == NULL)
        return (Microphone*)auDestroy((Audio*)self);
    
      self->max_silent_beats = 8;
    
      self->rhythm_onsets_len = 128;
      self->rhythm_onsets = calloc(self->rhythm_onsets_len, sizeof(*self->rhythm_onsets));
      if(self->rhythm_onsets == NULL)
        return (Microphone*)auDestroy((Audio*)self);

      btt_set_onset_tracking_callback  (self->btt, mic_onset_detected_callback, self);
      btt_set_beat_tracking_callback   (self->btt, mic_beat_detected_callback , self);
    
      if(pthread_mutex_init(&self->rhythm_generator_swap_mutex, NULL) != 0)
        return (Microphone*)auDestroy((Audio*)self);
    
      //should happen after mutex init
      self->rhythm = mic_set_rhythm_generator_index(self, 0);
    
      self->rhythm_thread_run_loop_running = 1;
      int error = pthread_create(&self->rhythm_thread, NULL, mic_rhythm_thread_run_loop, self);
      if(error != 0)
        return (Microphone*)auDestroy((Audio*)self);
    }
  
  //there should be a play callback that I can intercept and do this there.
  auPlay((Audio*)self->click);
  return self;
}

/*--------------------------------------------------------------------*/
void mic_onset_detected_callback(void* SELF, unsigned long long sample_time)
{
  Microphone* self = (Microphone*) SELF;
  
  pthread_mutex_lock(&self->rhythm_generator_swap_mutex);
  if(self->rhythm != NULL)
    rhythm_onset(self->rhythm, self->btt, sample_time);
  pthread_mutex_unlock(&self->rhythm_generator_swap_mutex);
  
  if(btt_get_tracking_mode(self->btt) <= BTT_ONSET_TRACKING)
    {
      click_click(self->click);
      //robot_send_message(self->robot, robot_cmd_tap, 1.0 /*strength*/);
      //fprintf(stderr, "onset\r\n");
    }

  self->silent_beat_count = 0;
}

/*--------------------------------------------------------------------*/
/* store each beat as a single click in an AIFF file so we can compare it to the original */
void mic_beat_detected_callback (void* SELF, unsigned long long sample_time)
{
  Microphone* self = (Microphone*) SELF;
  
  //mutex lock
  self->rhythm_onsets_index = 0;
  
  pthread_mutex_lock(&self->rhythm_generator_swap_mutex);
  if(self->rhythm != NULL)
    self->num_rhythm_onsets = rhythm_beat(self->rhythm, self->btt, sample_time, self->rhythm_onsets, self->rhythm_onsets_len);
  pthread_mutex_unlock(&self->rhythm_generator_swap_mutex);
  
  int i;
  float beat_period = btt_get_beat_period_audio_samples(self->btt) / (float)btt_get_sample_rate(self->btt);
  for(i=0; i<self->num_rhythm_onsets; i++)
    self->rhythm_onsets[i] *= round(beat_period * (1000000 / (double)MIC_RHYTHM_THREAD_RUN_LOOP_INTERVAL));
  
  self->beat_clock = 0;
  
  //if(self->play_beat_bell)
    {
      click_klop(self->click);
      robot_send_message(self->robot, robot_cmd_tap, 1.0 /*strength*/);
    }
  
  ++self->silent_beat_count;
  if(self->silent_beat_count > self->max_silent_beats)
      btt_set_tracking_mode(self->btt, BTT_COUNT_IN_TRACKING);
}

/*--------------------------------------------------------------------*/
Microphone* mic_destroy(Microphone* self)
{
  if(self != NULL)
    {
      if(self->rhythm_thread != NULL)
        {
          self->rhythm_thread_run_loop_running = 0;
          pthread_join(self->rhythm_thread, NULL);
        }
    
      btt_destroy(self->btt);
      robot_destroy(self->robot);
      auDestroy((Audio*)self->click);
    }
    
  return (Microphone*) NULL;
}

/*--------------------------------------------------------------------*/
BTT*           mic_get_btt        (Microphone* self)
{
  return self->btt;
}

/*--------------------------------------------------------*/
void mic_message_recd_from_robot(void* self, char* message, robot_arg_t args[], int num_args)
{
  //robot_debug_print_message(message, args, num_args);
  
  /*
  switch(kiki_hash_message(message))
    {
      case kiki_hash_mmap_addr:
        fprintf(stderr, "kiki just replied to a mmap request\r\n");
        break;
      
      default: break;
    }
  */
}

/*--------------------------------------------------------------------*/
Rhythm* mic_set_rhythm_generator       (Microphone* self, rhythm_new_funct constructor)
{
  int i;
  self->rhythm_constructor_index = -1;
  for(i=0; i<rhythm_num_constructors; i++)
    if(constructor == rhythm_constructors[i])
      {self->rhythm_constructor_index = i; break;}
  
  pthread_mutex_lock(&self->rhythm_generator_swap_mutex);
  if(self->rhythm != NULL)
    self->rhythm = rhythm_destroy(self->rhythm);
  self->rhythm = constructor(self->btt);
  pthread_mutex_unlock(&self->rhythm_generator_swap_mutex);
  
  return self->rhythm;
}

/*--------------------------------------------------------------------*/
Rhythm* mic_set_rhythm_generator_index (Microphone* self, int index)
{
  if(rhythm_num_constructors <= 0) return self->rhythm;
  
  if(index < 0) index = 0;
  if(index >= rhythm_num_constructors) index = rhythm_num_constructors-1;
  
  return mic_set_rhythm_generator(self, rhythm_constructors[index]);
}

/*--------------------------------------------------------------------*/
int mic_get_rhythm_generator_index (Microphone* self)
{
  return self->rhythm_constructor_index;
}

/*--------------------------------------------------------------------*/
Rhythm* mic_get_rhythm_generator       (Microphone* self)
{
  return self->rhythm;
}

/*--------------------------------------------------------------------*/
void* mic_rhythm_thread_run_loop (void* SELF)
{
  Microphone* self = (Microphone*)SELF;
  timestamp_microsecs_t start = timestamp_get_current_time();
  
  while(self->rhythm_thread_run_loop_running)
    {
      if(self->rhythm_onsets_index < self->num_rhythm_onsets)
        if(self->beat_clock >= self->rhythm_onsets[self->rhythm_onsets_index])
          {
            click_click(self->click);
            robot_send_message(self->robot, robot_cmd_tap, 1.0 /*strength*/);
            ++self->rhythm_onsets_index;
          }
        
      ++self->beat_clock; //reset at the begining of each beat
      ++self->thread_clock; //never reset
    
      while((timestamp_get_current_time() - start) < (self->thread_clock*MIC_RHYTHM_THREAD_RUN_LOOP_INTERVAL - 25))
        usleep(50);
    }
  return NULL;
}

/*--------------------------------------------------------------------*/
int mic_audio_callback(void* SELF, auSample_t* buffer, int num_frames, int num_channels)
{
  Microphone* self = (Microphone*)SELF;
  int frame, channel;
  
  //mix to mono without correcting amplitude
  for(frame=0; frame<num_frames; frame++)
    for(channel=1; channel<num_channels; channel++)
      buffer[frame] += buffer[frame * num_channels + channel];
  
  btt_process(self->btt, buffer, num_frames);
  return  num_frames;
}
