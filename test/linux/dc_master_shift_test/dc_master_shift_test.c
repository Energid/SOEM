/** \file
 * \brief Example code for Simple Open EtherCAT master DC master shift test
 *
 * Usage : master_shift_test [ifname1] [cycletime]
 * ifname is NIC interface, f.e. enp2s0
 * cycletime is in ms, f.e. 2000
 *
 * This is a minimal test.
 *
 * (c)Arthur Ketels 2010 - 2011
 * (c)Changshuo Li
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>

#include "ethercat.h"

#define EC_TIMEOUTMON 500

char IOmap[4096];
int expectedWKC;
volatile int wkc;
uint8 currentgroup = 0;

boolean run_realtime = FALSE;

void master(char *ifname, int cycletime)
{
   printf("Starting master shift test\n");
   ecx_context.manualstatechange = 1;

   /* initialise SOEM, bind socket to ifname */
   if (ec_init(ifname) <= 0)
   {
      printf("No socket connection on %s\nExcecute as root\n", ifname);
      return;
   }
   printf("ec_init on %s succeeded.\n", ifname);

   /* find and auto-config slaves */
   int nSlave = ec_config_init(FALSE);
   if (nSlave != 1)
   {
      printf("%d slaves found, expect 1!\n", *ecx_context.slavecount);
      return;
   }

   /* wait for all slaves to reach PRE_OP state */
   printf("Waiting for network transition to PRE_OP state ... ");
   ecx_context.slavelist[0].state = EC_STATE_PRE_OP;
   ec_writestate(0);
   ec_statecheck(0, EC_STATE_PRE_OP, EC_TIMEOUTSTATE * 4);
   printf("Done.\n");
   printf("%d slaves found and configured.\n", ec_slavecount);
   printf("%s, %04x, %04x, %08x\n", ec_slave[1].name, ec_slave[1].eep_man, ec_slave[1].eep_id, ec_slave[1].eep_rev);

   /* configure slave, PDO and others */
   {
      uint8 u8;
      uint16 u16;
      uint32 u32;

      u8 = 0;
      ec_SDOwrite(1, 0x1c12, 0x0, 0, 1, &u8, EC_TIMEOUTRXM);
      u8 = 0;
      ec_SDOwrite(1, 0x1600, 0x0, 0, 1, &u8, EC_TIMEOUTRXM);
      u32 = 0x607a0020; // target position
      ec_SDOwrite(1, 0x1600, 0x1, 0, 4, &u32, EC_TIMEOUTRXM);
      u32 = 0x60400010; // control word
      ec_SDOwrite(1, 0x1600, 0x2, 0, 4, &u32, EC_TIMEOUTRXM);
      u32 = 0x60600008; // modes of operation
      ec_SDOwrite(1, 0x1600, 0x3, 0, 4, &u32, EC_TIMEOUTRXM);
      u8 = 3;
      ec_SDOwrite(1, 0x1600, 0x0, 0, 1, &u8, EC_TIMEOUTRXM);
      u16 = 0x1600;
      ec_SDOwrite(1, 0x1c12, 0x1, 0, 2, &u16, EC_TIMEOUTRXM);
      u8 = 1;
      ec_SDOwrite(1, 0x1c12, 0x0, 0, 1, &u8, EC_TIMEOUTRXM);

      u8 = 0;
      ec_SDOwrite(1, 0x1c13, 0x0, 0, 1, &u8, EC_TIMEOUTRXM);
      u8 = 0;
      ec_SDOwrite(1, 0x1a00, 0x0, 0, 1, &u8, EC_TIMEOUTRXM);
      u32 = 0x60640020; // actual position
      ec_SDOwrite(1, 0x1a00, 0x1, 0, 4, &u32, EC_TIMEOUTRXM);
      u32 = 0x60410010; // status word
      ec_SDOwrite(1, 0x1a00, 0x2, 0, 4, &u32, EC_TIMEOUTRXM);
      u32 = 0x60610008; // modes of operation display
      ec_SDOwrite(1, 0x1a00, 0x3, 0, 4, &u32, EC_TIMEOUTRXM);
      u32 = 0x603f0010; // error code
      ec_SDOwrite(1, 0x1a00, 0x4, 0, 4, &u32, EC_TIMEOUTRXM);
      u8 = 4;
      ec_SDOwrite(1, 0x1a00, 0x0, 0, 1, &u8, EC_TIMEOUTRXM);
      u16 = 0x1a00;
      ec_SDOwrite(1, 0x1c13, 0x1, 0, 2, &u16, EC_TIMEOUTRXM);
      u8 = 1;
      ec_SDOwrite(1, 0x1c13, 0x0, 0, 1, &u8, EC_TIMEOUTRXM);

      // u32 = 125000;
      // ec_SDOwrite(1, 0x1c32, 0x3, 0, 4, &u32, EC_TIMEOUTRXM);
      // u32 = -1;
      // ec_SDOwrite(1, 0x1c33, 0x3, 0, 4, &u32, EC_TIMEOUTRXM);
   }

   ec_config_map(&IOmap);

   printf("segments : %d : %d %d %d %d\n", ec_group[0].nsegments,
          ec_group[0].IOsegment[0], ec_group[0].IOsegment[1],
          ec_group[0].IOsegment[2], ec_group[0].IOsegment[3]);

   /* wait for all slaves to reach SAFE_OP state */
   printf("Waiting for network transition to SAFE_OP state ... ");
   ecx_context.slavelist[0].state = EC_STATE_SAFE_OP;
   ec_writestate(0);
   ec_statecheck(0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE * 4);
   printf("Done.\n");

   (void)cycletime;
   ec_configdc();
   ec_dcsync0(1, TRUE, cycletime * 1000, 250 * 1000);
   run_realtime = TRUE;
   ec_readstate();
   printf("%d\n", ec_slave[1].state);

   printf("Request operational state for all slaves\n");
   expectedWKC = (ec_group[0].outputsWKC * 2) + ec_group[0].inputsWKC;
   printf("Calculated workcounter %d\n", expectedWKC);

   while (1)
   {
      ec_readstate();
      // if (ecx_context.slavelist[1].state == EC_STATE_SAFE_OP + EC_STATE_ERROR)
      // {
      //    /* request OP state for all slaves */
      //    ec_slave[1].state = EC_STATE_SAFE_OP + EC_STATE_ACK;
      //    ec_writestate(1);
      // }
      if (ecx_context.slavelist[1].state == EC_STATE_SAFE_OP)
      {
         /* request OP state for all slaves */
         ec_slave[1].state = EC_STATE_OPERATIONAL;
         ec_writestate(1);
      }

      int sz;
      // uint8 u8;
      uint16 u16b, u16c, u16d;
      uint32 u32;

      sz = 4;
      ec_SDOread(1, 0x1c32, 0x3, FALSE, &sz, &u32, EC_TIMEOUTRXM);
      sz = 2;
      ec_SDOread(1, 0x1c32, 0xb, FALSE, &sz, &u16b, EC_TIMEOUTRXM);
      sz = 2;
      ec_SDOread(1, 0x1c32, 0xc, FALSE, &sz, &u16c, EC_TIMEOUTRXM);
      sz = 2;
      ec_SDOread(1, 0x1c32, 0xd, FALSE, &sz, &u16d, EC_TIMEOUTRXM);
      // printf("%d, %d, %d, %d, %d\n", ec_slave[1].state, u32, u16b, u16c, u16d);
      osal_usleep(20000);
   }

   /* request INIT state for all slaves */
   printf("\nRequest init state for all slaves\n");
   ec_slave[0].state = EC_STATE_INIT;
   ec_writestate(0);

   /* stop SOEM, close socket */
   printf("End master shift test, close socket\n");
   ec_close();
}

/* add ns to timespec */
void add_timespec(struct timespec *ts, int64 addtime)
{
   int64 nsec;

   nsec = ts->tv_nsec + addtime;
   ts->tv_nsec = nsec % 1000000000;
   ts->tv_sec += (nsec - ts->tv_nsec) / 1000000000;
}

/* PI calculation to get linux time synced to DC time */
void ec_sync(int64 reftime, int64 cycletime, int64 *offsettime)
{
   int64 delta, integral = 0;
   /* set linux sync point 50us later than DC sync, just as example */
   delta = (reftime - 50000) % cycletime;
   if (delta > (cycletime / 2))
   {
      delta = delta - cycletime;
   }
   if (delta > 0)
   {
      integral++;
   }
   if (delta < 0)
   {
      integral--;
   }
   *offsettime = -(delta / 100) - (integral / 20);
}

/* realtime thread */
void realtime(void *ptr)
{
   struct timespec ts, tleft;
   int ht;
   int64 cycletime;
   int64 toff = 0;

   clock_gettime(CLOCK_MONOTONIC, &ts);
   ht = (ts.tv_nsec / 1000000) + 1; /* round to nearest ms */
   ts.tv_nsec = ht * 1000000;
   cycletime = *(int *)ptr * 1000; /* cycletime in ns */

   int64 dcT = 0, dcC = 0, acc = 0;

   int32 ap = 0, tp = 0, dir = 1;
   uint16 sw = 0, cw = 0, ec = 0;
   int8 mod = 0, mo = 0, cnt = 0;

   ec_send_processdata();
   while (1)
   {
      /* calculate next cycle start */
      add_timespec(&ts, cycletime + toff);
      /* wait to cycle start */
      clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, &tleft);

      if (run_realtime)
      {
         wkc = ec_receive_processdata(EC_TIMEOUTRET);

         if (ec_slave[0].hasdc)
         {
            if (ec_DCtime < dcT - (dcC << 32))
            {
               ++dcC;
            }
            dcT = ec_DCtime + (dcC << 32);
            /* calulate toff to get linux time and DC synced */
            ec_sync(dcT, cycletime, &toff);
         }

         memcpy(&ap, ec_slave[0].inputs, 4);
         memcpy(&sw, ec_slave[0].inputs + 4, 2);
         memcpy(&mod, ec_slave[0].inputs + 6, 1);
         memcpy(&ec, ec_slave[0].inputs + 7, 2);
         // printf("dc: %ld, Targets: %d, %04x, %d, Actuals: %d, %04x, %d, %d\n", ec_DCtime % cycletime, mo, cw, tp, mod, sw, ap, ec);
         acc += toff;
         printf("%010ld, %ld, %ld, %ld, %ld\n", ec_DCtime, dcC, dcT, ts.tv_sec * 1000000000 + ts.tv_nsec, acc);

         if (mod != 8)
         {
            mo = 8;
         }
         else
         {
            if ((sw & 0x4f) == 8)
            {
               if (++cnt > 10)
               {
                  cnt = 0;
                  if (cw == 128)
                     cw = 0;
                  else
                     cw = 128;
               }
            }
            else if ((sw & 0x4f) == 64)
               cw = 6;
            else if ((sw & 0x6f) == 33)
               cw = 7;
            else if ((sw & 0x6f) == 35)
               cw = 15;
         }

         if ((sw & 0x6f) == 39)
         {
            if (ap > 100000 && dir > 0)
            {
               dir = -1;
            }
            else if (ap < -100000 && dir < 0)
            {
               dir = 1;
            }
            tp = tp + dir * 50;
         }
         else
         {
            tp = ap;
         }

         memcpy(ec_slave[0].outputs, &tp, 4);
         memcpy(ec_slave[0].outputs + 4, &cw, 2);
         memcpy(ec_slave[0].outputs + 6, &mo, 1);
         ec_send_processdata();
      }
   }
}

int main(int argc, char *argv[])
{
   printf("SOEM (Simple Open EtherCAT Master)\ndc master shift test\n");

   if (argc > 2)
   {
      int cycletime = atoi(argv[2]);

      pthread_t rtThread;
      osal_thread_create(&rtThread, 0, &realtime, (void *)&cycletime);
      struct sched_param param = {.sched_priority = 99};
      pthread_setschedparam(rtThread, SCHED_RR, &param);

      /* start cyclic part */
      master(argv[1], cycletime);
   }
   else
   {
      printf("Usage: master_shift_test ifname cycletime\nifname = enp2s0 for example\ncyctime in us\n");
   }

   printf("End program\n");
   return (0);
}
