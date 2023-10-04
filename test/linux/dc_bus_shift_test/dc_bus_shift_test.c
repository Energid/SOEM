/** \file
 * \brief Example code for Simple Open EtherCAT master running
 * a FSoE Master Application.
 *
 * (c)Andreas Karlsson 2019
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>

#include "ethercat.h"

typedef enum
{
   STOP,
   INIT,
   CONFIG,
   SYNC_SLAVES,
   SYNC_MASTER,
   RUN
} APP_STATE;

APP_STATE appState = INIT;

char IOmap[512];
volatile int global_wkc;
int realtime_running = 0;

void master(char *ifname, int cycletime)
{
   printf("Starting EtherCAT master\n");

   //------------------------------------------------------
   appState = INIT;
   printf("\n----- Init -----\n");

   ecx_context.manualstatechange = 1;

   // initialise SOEM, bind socket to ifname
   if (ec_init(ifname) <= 0)
   {
      printf("No socket connection on %s\nExcecute as root\n", ifname);
      return;
   }
   printf("EtherCAT init on %s succeeded.\n", ifname);

   //------------------------------------------------------
   appState = CONFIG;
   printf("\n----- Config -----\n");

   // find and auto-config slaves
   int nSlave = ec_config_init(FALSE);
   if (nSlave != 1)
   {
      printf("%d slaves found, expect 1\n", ec_slavecount);
      return;
   }

   // show slave info
   for (int i = 1; i <= ec_slavecount; i++)
   {
      printf("Slave %d. \n\t%s \n\t0x%04x, 0x%04x, 0x%08x\n",
             i,
             ec_slave[i].name,
             ec_slave[i].eep_man,
             ec_slave[i].eep_id,
             ec_slave[i].eep_rev);
   }

   // transit to PRE_OP state
   ec_slave[0].state = EC_STATE_PRE_OP;
   ec_writestate(0);
   uint16 st = ec_statecheck(0, EC_STATE_PRE_OP, EC_TIMEOUTSTATE * 4);
   printf("Network state to PRE_OP (%d)\n", st);

   // configure DC
   ec_configdc();
   for (int i = 1; i <= ec_slavecount; i++)
   {
      printf("Slave %d. \n\tport delay %d\n", i, ec_slave[1].pdelay);
   }

   // configure slave, PDO and others
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

      if (ec_slave[1].hasdc > 0)
      {
         ec_dcsync0(1,
                    TRUE,
                    cycletime * 1000,
                    200 * 1000);
      }
   }

   memset(IOmap, 0, sizeof(IOmap));
   ec_config_map(&IOmap);
   printf("%d slaves found and configured.\n", ec_slavecount);

   // transit to SAFE_OP state
   ec_slave[0].state = EC_STATE_SAFE_OP;
   ec_writestate(0);
   st = ec_statecheck(0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE * 4);
   printf("Network state to SAFE_OP (%d)\n", st);
   realtime_running = 1;

   // read individual slave state
   ec_readstate();
   for (int i = 1; i <= ec_slavecount; i++)
   {
      printf("Slave %d. \n\t%s \n\tOutput size:%3dbits \n\tInput size:%3dbits \n\tState:%2d \n\tdelay:%d.%d\n",
             i,
             ec_slave[i].name,
             ec_slave[i].Obits,
             ec_slave[i].Ibits,
             ec_slave[i].state,
             (int)ec_slave[i].pdelay,
             ec_slave[i].hasdc);
      printf("\tOut:%8p,%4d \n\tIn: %8p,%4d\n",
             (void *)ec_slave[i].outputs,
             (int)ec_slave[i].Obytes,
             (void *)ec_slave[i].inputs,
             (int)ec_slave[i].Ibytes);
   }

   int expectedWKC = (ec_group[0].outputsWKC * 2) + ec_group[0].inputsWKC;
   printf("Calculated workcounter %d\n", expectedWKC);

   //------------------------------------------------------
   appState = SYNC_SLAVES;
   printf("\n----- Sync Slaves -----\n");

   // wait for local diff in all slaves to reach a minimal value
   int cnt = 0, dcsysdiff;

   // Release the task to start now that the start time is set
   osal_usleep(1000);

   // Check for DC status for slaves only
   do
   {
      ec_BRD(0x0000, ECT_REG_DCSYSDIFF, sizeof(dcsysdiff), &dcsysdiff, EC_TIMEOUTSAFE);
      printf("%d, Dc diff slave %12u\n", cnt, (dcsysdiff & 0x7FFFFFFF));
      osal_usleep(EC_TIMEOUTSAFE);
      cnt++;
   } while (((dcsysdiff & 0x7FFFFFFF) > (0x1ff)));
   printf("\nSync slaves function run %d times\n", cnt);

   //------------------------------------------------------
   appState = SYNC_MASTER;
   printf("\n----- Sync Master -----\n");

   // wait for the Master clock and DC clock to be synced
   // Let sync of master clock start
   osal_usleep(100000);

   // Check for DC status for slaves only
   cnt = 0;
   do
   {
      ec_BRD(0x0000, ECT_REG_DCSYSDIFF, sizeof(dcsysdiff), &dcsysdiff, EC_TIMEOUTSAFE);
      if (cnt % 10 == 0)
      {
         printf("%d, Dc diff master %12u\n", cnt, (dcsysdiff & 0x7FFFFFFF));
      }
      osal_usleep(EC_TIMEOUTSAFE);
      cnt++;
   } while (((dcsysdiff & 0x7FFFFFFF) > (0x1fff)));
   printf("\nSync master to slave dc ref function run %d times\n", cnt);

   // transit to OP state
   ec_slave[0].state = EC_STATE_OPERATIONAL;
   ec_writestate(0);
   st = ec_statecheck(0, EC_STATE_OPERATIONAL, EC_TIMEOUTSTATE * 4);
   printf("Network state to OP (%d)\n", st);

   //------------------------------------------------------
   appState = RUN;
   printf("\n----- Run -----\n");

   while (1)
   {
      osal_usleep(20000);
      if (global_wkc < expectedWKC)
      {
         printf("WKC : %d < expectedWKC : %d\n", global_wkc, expectedWKC);
      }

      ec_readstate();
      for (int i = 1; i <= ec_slavecount; i++)
      {
         if (ec_slave[i].state != EC_STATE_OPERATIONAL)
         {
            printf("Slave %d State=0x%2.2x StatusCode=0x%4.4x : %s\n",
                   i, ec_slave[i].state,
                   ec_slave[i].ALstatuscode,
                   ec_ALstatuscode2string(ec_slave[i].ALstatuscode));
         }
      }

      if (ecx_context.slavelist[1].state == EC_STATE_SAFE_OP)
      {
         /* request OP state for all slaves */
         ec_slave[1].state = EC_STATE_OPERATIONAL;
         ec_writestate(1);
      }
   }

   //------------------------------------------------------
   printf("\n----- Close -----\n");
   // transit to OP state
   ec_slave[0].state = EC_STATE_INIT;
   ec_writestate(0);

   // stop SOEM, close socket
   ec_close();
   printf("End simple test, close socket\n");

   return;
}

/* add ns to timespec */
void add_timespec(struct timespec *ts, int64 addtime)
{
   int64 nsec;

   nsec = ts->tv_nsec + addtime;
   ts->tv_nsec = nsec % 1000000000;
   ts->tv_sec += (nsec - ts->tv_nsec) / 1000000000;
}

void realtime(void *ptr)
{
   // cycletime in ns
   int64 cycletime = *(int *)ptr * 1000;

   //------------------------------------------------------
   while (appState < SYNC_SLAVES)
   {
      osal_usleep(1000);
   }

   //------------------------------------------------------
   while (appState == SYNC_SLAVES)
   {
      if (realtime_running)
      {
         ec_send_processdata();
         global_wkc = ec_receive_processdata(EC_TIMEOUTRET);
      }
   }

   //------------------------------------------------------
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);

   uint64 external_ref_clock;
   ec_FPRD(ec_slave[ec_group[0].DCnext].configadr,
           ECT_REG_DCSYSTIME,
           sizeof(external_ref_clock),
           &external_ref_clock,
           EC_TIMEOUTRET);
   int64 base_time_diff = external_ref_clock - ts.tv_sec * 1000000000 - ts.tv_nsec;
   printf("Master time %ld, slave time %ld, diff %ld\n",
          ts.tv_sec * 1000000000 + ts.tv_nsec,
          external_ref_clock,
          base_time_diff);

   ts.tv_nsec = ((ts.tv_nsec / cycletime) + 1) * cycletime;
   printf("Master time ref %ld, %ld\n", ts.tv_sec, ts.tv_nsec);

   int32 ap = 0, tp = 0, dir = 1;
   uint16 sw = 0, cw = 0, ec = 0;
   int8 mod = 0, mo = 8, cnt = 0;
   // int32 tDiff = 0;
   // uint64 tSys = 0;

   // struct timespec tt;
   while (realtime_running)
   {
      // wait to cycle start
      clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);

      if (appState > CONFIG)
      {
         if (appState > SYNC_SLAVES)
         {
            /* Set ref time bound to FPGA heart beat */
            external_ref_clock = (ts.tv_sec * 1000000000 + ts.tv_nsec + base_time_diff); // & 0xFFFFFFFF;
            ec_FPWR(ec_slave[ec_group[0].DCnext].configadr,
                    ECT_REG_DCSYSTIME,
                    sizeof(external_ref_clock),
                    &external_ref_clock,
                    EC_TIMEOUTRET);

            // clock_gettime(CLOCK_MONOTONIC, &tt);
            // printf("%ld, %ld\n", external_ref_clock, ts.tv_sec * 1000000000 + ts.tv_nsec);

            // ec_BRD(0x0000, ECT_REG_DCSYSDIFF, sizeof(tDiff), &tDiff, EC_TIMEOUTRET);
            // ec_BRD(0x0000, ECT_REG_DCSYSTIME, sizeof(tSys), &tSys, EC_TIMEOUTRET);
            // printf("diff: %c%5d, sys: %10lu, tm: %lu.%09lu, sd: %lu.%09lu\n",
            //        (tDiff & 0x80000000) ? '-' : '+', tDiff & 0x7fffffff,
            //        tSys,
            //        tt.tv_sec, tt.tv_nsec,
            //        ts.tv_sec, ts.tv_nsec);
         }

         ec_send_processdata();
         global_wkc = ec_receive_processdata(EC_TIMEOUTRET);

         if (appState > SYNC_MASTER)
         {
            memcpy(&ap, ec_slave[0].inputs, 4);
            memcpy(&sw, ec_slave[0].inputs + 4, 2);
            memcpy(&mod, ec_slave[0].inputs + 6, 1);
            memcpy(&ec, ec_slave[0].inputs + 7, 2);
            // printf("dc: %ld, Targets: %d, %04x, %d, Actuals: %d, %04x, %d, %04x\n", ec_DCtime % cycletime, mo, cw, tp, mod, sw, ap, ec);

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
         }
      }

      // calculate next cycle start
      add_timespec(&ts, cycletime);
   }
}

int main(int argc, char *argv[])
{
   printf("SOEM (Simple Open EtherCAT Master) DC bus shift test\n");

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
      printf("Usage: bus_shift_test ifname cycletime\nifname = enp2s0 for example\ncyctime in us");
   }

   printf("End program\n");
   return (0);
}