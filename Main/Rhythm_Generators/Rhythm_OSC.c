/*
 *  Rhythm_OSC.c
 *  To use:
 *    if you want to make a module named "foo",
 *    search and replace "OSC" with Foo (case sensitive)
 *    and "OSC" with "foo" (case sensitive).
 *    Then declare any variables you need in opaque_rhythm_foo_struct,
 *    initalize them in rhythm_foo_new,
 *    free them if necessary in rhythm_foo_destroy.
 *    Musical onsets will be reported in rhythm_OSC_onset
 *      (this is called automatically by the onset tracker)
 *    You should generate one beat of rhythm at a time, in rhythm_OSC_beat
 *      (this is called automatically by the beat tracker at the begining of each beat)
 *  mkrzyzan@asu.edu
 */

#include "Rhythm_Generators.h"
#include <pthread.h>
#include <math.h>

#include "../extras/Network.h" //calloc
#include "../extras/OSC.h" //calloc

#define  OSC_BUFFER_SIZE 512
#define  OSC_VALUES_BUFFER_SIZE 64
#define  OSC_SEND_PORT   9000
#define  OSC_RECV_PORT   9001
#define  OSC_ONSET_BUFFER_SIZE 64 //no bigger than OSC_VALUES_BUFFER_SIZE

/*--------------------------------------------------------------------*/
void*        rhythm_osc_destroy (void*);
const char*  rhythm_osc_name    (void*);
void         rhythm_osc_onset   (void*, BTT*, unsigned long long);
int          rhythm_osc_beat    (void*, BTT*, unsigned long long, rhythm_onset_t*, int);

void*        rhythm_osc_recv_thread_run_loop(void* SELF);
/*--------------------------------------------------------------------*/
typedef struct opaque_rhythm_osc_struct
{
  RHYTHM_GENERATOR_SUBCLASS_GUTS ;
  
  /* add instance variables below here */
  Network* net;
  double metronome_tempo;
  BTT* btt;
  char* osc_send_buffer;
  char* osc_recv_buffer;
  oscValue_t* osc_values_buffer;
  rhythm_onset_t* onsets;
  int num_onsets;
  
  int robot_id;
  int beat_id;
  
  pthread_t osc_recv_thread;
  pthread_mutex_t onset_buffer_mutex;
}Rhythm_OSC;

/*--------------------------------------------------------------------*/
Rhythm* rhythm_osc_new(BTT* beat_tracker)
{
  Rhythm_OSC* self = (Rhythm_OSC*) calloc(1, sizeof(*self));
  
  if(self != NULL)
    {
      self->destroy = rhythm_osc_destroy;
      self->name    = rhythm_osc_name;
      self->onset   = rhythm_osc_onset;
      self->beat    = rhythm_osc_beat;
    
      self->btt = beat_tracker;
    
      /* initalize instance variables below here */
      /* return rhythm_osc_destroy(self) on failure */
      self->osc_send_buffer = calloc(1, OSC_BUFFER_SIZE);
      if(!self->osc_send_buffer) return rhythm_osc_destroy(self);
      self->osc_recv_buffer = calloc(1, OSC_BUFFER_SIZE);
      if(!self->osc_recv_buffer) return rhythm_osc_destroy(self);
      self->osc_values_buffer = calloc(sizeof(*self->osc_values_buffer), OSC_VALUES_BUFFER_SIZE);
      if(!self->osc_values_buffer) return rhythm_osc_destroy(self);
      self->onsets = calloc(sizeof(*self->onsets), OSC_ONSET_BUFFER_SIZE);
      if(!self->onsets) return rhythm_osc_destroy(self);

      self->net  = net_new();
      if(!self->net) return rhythm_osc_destroy(self);
      if(!net_udp_connect (self->net, OSC_RECV_PORT))
        return rhythm_osc_destroy(self);
    
      //self->robot_id = random() & 0x7FFFFFFF; //keep the first bit clear to avoid sign confusion
      rhythm_osc_set_robot_osc_id(self, 0);
      self->beat_id = 0;

      if(pthread_mutex_init(&self->onset_buffer_mutex, NULL) != 0)
        return rhythm_osc_destroy(self);
    
      int error = pthread_create(&self->osc_recv_thread, NULL, rhythm_osc_recv_thread_run_loop, self);
      if(error != 0)
        return rhythm_osc_destroy(self);
    }
  
  return (Rhythm*)self;
}

/*--------------------------------------------------------------------*/
uint32_t rhythm_osc_sample_time_to_millis(BTT* beat_tracker, unsigned long long sample_time)
{
  //overflows after about 50 days
  double  sample_rate = btt_get_sample_rate(beat_tracker);
  return (uint32_t) round(1000.0 * (sample_time / (long double) sample_rate));
}

/*--------------------------------------------------------------------*/
void*      rhythm_osc_destroy (void* SELF)
{
  Rhythm_OSC* self = (Rhythm_OSC*)SELF;
  if(self != NULL)
    {
      /* free any malloc-ed instance variables here */
    
      if(self->osc_recv_thread != 0)
        {
          pthread_cancel(self->osc_recv_thread);
          pthread_join(self->osc_recv_thread, NULL);
        }
      free(self->osc_send_buffer);
      free(self->osc_recv_buffer);
      net_disconnect(self->net);
      net_destroy(self->net);
      free(self);
    }
  return (Rhythm*) NULL;
}

/*--------------------------------------------------------------------*/
void   rhythm_osc_set_robot_osc_id               (void* SELF, int id)
{
  Rhythm_OSC* self = (Rhythm_OSC*)SELF;
  self->robot_id = id;
}

/*--------------------------------------------------------------------*/
int    rhythm_osc_get_robot_osc_id               (void* SELF)
{
  Rhythm_OSC* self = (Rhythm_OSC*)SELF;
  return  self->robot_id;
}

/*--------------------------------------------------------------------*/
const char*  rhythm_osc_name    (void* SELF)
{
  /* just return the name of the module for display */
  static const char* name = "OSC";
  return name;
}

/*--------------------------------------------------------------------*/
void         rhythm_osc_onset   (void* SELF, BTT* beat_tracker, unsigned long long sample_time)
{
  /* This will be called whenever an onset is detected
     sample_time tells you how many samples into the audio stream the
      onset occurred, and is probably a few milliseconds in the past
     you can do whatever you want with this information, or ignore it.
  */
  Rhythm_OSC* self = (Rhythm_OSC*)SELF;
  unsigned millis = rhythm_osc_sample_time_to_millis(beat_tracker, sample_time);
  int num_bytes = oscConstruct(self->osc_send_buffer, OSC_BUFFER_SIZE, "/onset", "i", millis);
  if(num_bytes > 0)
    net_udp_send(self->net, self->osc_send_buffer, num_bytes, "255.255.255.255", OSC_SEND_PORT);
}

/*----------------------------------------------------------*/
void debug_print(rhythm_onset_t* rhythm, int N)
{
  int i;
  for(i=0; i<N; i++)
    fprintf(stderr, "rhythm[%i] = {time: %0.2f\tstrength: %0.2f\tclass:%i}\r\n",
        i, rhythm[i].beat_time, rhythm[i].strength, rhythm[i].timbre_class);
  fprintf(stderr, "\r\n");
}


/*--------------------------------------------------------------------*/
int          rhythm_osc_beat    (void* SELF, BTT* beat_tracker, unsigned long long sample_time, rhythm_onset_t* returned_rhythm, int returned_rhythm_maxlen)
{
  /* This will be called whenever a beat is detected.
     You should generate one beat of rhythm and write it into returned_rhythm.
     returned_rhythm should contain values on [0~1) in ascending order
     that specify where within the beat the onset should occur. 0 indicates right now, 0.5
     is midway through the beat, etc. [0, 0.25, 0.5, 0.75] would be four 16th notes.
   
     Return the number of values written into returned_rhythm, not to exceed returned_rhythm_maxlen.
     sample_time tells you how many samples into the audio stream the beat occurrs,
     and might be in the past or future.
  */
  Rhythm_OSC* self = (Rhythm_OSC*)SELF;
  
  int i;
  
  pthread_mutex_lock(&self->onset_buffer_mutex);
  int n = (returned_rhythm_maxlen < self->num_onsets) ? returned_rhythm_maxlen : self->num_onsets;
  for(i=0; i<n; i++)
    {
      returned_rhythm[i].beat_time    = self->onsets[i].beat_time;
      returned_rhythm[i].strength     = self->onsets[i].strength;
      returned_rhythm[i].timbre_class = self->onsets[i].timbre_class;
    }
  self->num_onsets = 0;
  pthread_mutex_unlock(&self->onset_buffer_mutex);

  ++self->beat_id;
  unsigned millis = rhythm_osc_sample_time_to_millis(beat_tracker, sample_time);
  int num_bytes = oscConstruct(self->osc_send_buffer, OSC_BUFFER_SIZE, "/beat", "iii", millis, self->robot_id, self->beat_id);
  if(num_bytes > 0)
    net_udp_send(self->net, self->osc_send_buffer, num_bytes, "255.255.255.255", OSC_SEND_PORT);
  
  btt_set_metronome_bpm(self->btt, self->metronome_tempo);
  
  return i;
}

/*--------------------------------------------------------------------*/
void* rhythm_osc_recv_thread_run_loop(void* SELF)
{
  Rhythm_OSC* self = (Rhythm_OSC*)SELF;
  char senders_address[16];
  char *osc_address, *osc_type_tag;
  
  for(;;)
  {
    int num_valid_bytes = net_udp_receive (self->net, self->osc_recv_buffer, OSC_BUFFER_SIZE, senders_address);
    if(num_valid_bytes < 0)
      continue; //return NULL ? 
  
    int num_osc_values = oscParse(self->osc_recv_buffer, num_valid_bytes, &osc_address, &osc_type_tag, self->osc_values_buffer, OSC_VALUES_BUFFER_SIZE);
    if(num_osc_values < 0)
        continue;
  
    uint32_t address_hash = oscHash((unsigned char*)osc_address);
    if(address_hash == 3170568888) // '/rhythm'
      {
        // /rhythm <int>robot_id <int>beat_id <float 0~1>time_1 <int 0~127>midi_note_number_1 <float 0~1>strength_1 <float 0~1>time_2 <int 0~127>midi_note_number_2 <float 0~1>strength_2 ...
      
        if(num_osc_values < 2)     continue; //must have robot_id and beat_id
        if((num_osc_values-2) % 3) continue; //after that, must have onset triplets
        int robot_id = oscValueAsInt(self->osc_values_buffer[0], osc_type_tag[0]);
        int beat_id  = oscValueAsInt(self->osc_values_buffer[1], osc_type_tag[1]);
      
        //fprintf(stderr, "requested_beat: %i\treceived_beat: %i\r\n", self->beat_id, beat_id);
      
        if((robot_id != self->robot_id) || (beat_id != self->beat_id))
          continue;
      
        int i, j=0;
        pthread_mutex_lock(&self->onset_buffer_mutex);
        for(i=2; i<num_osc_values; i+=3)
          {
            self->onsets[j].beat_time    = oscValueAsFloat(self->osc_values_buffer[i+0], osc_type_tag[i+0]);
            self->onsets[j].timbre_class = oscValueAsInt  (self->osc_values_buffer[i+1], osc_type_tag[i+1]);
            self->onsets[j].strength     = oscValueAsFloat(self->osc_values_buffer[i+2], osc_type_tag[i+2]);
            ++j;
          }
        self->num_onsets = j;
        rhythm_sort_by_onset_time(self->onsets, self->num_onsets);
        pthread_mutex_unlock(&self->onset_buffer_mutex);
      }
    else if(address_hash == 3352467465) // '/tempo'
      {
        //this only has any effect in btt metronome mode
        if(num_osc_values == 1)
          self->metronome_tempo = oscValueAsFloat(self->osc_values_buffer[0], osc_type_tag[0]);
          //there really isn't any sense in setting it more than once per beat
          //btt_set_metronome_bpm(self->btt, oscValueAsFloat(self->osc_values_buffer[0], osc_type_tag[0]));
      }
  }
}

