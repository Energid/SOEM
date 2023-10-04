/** \file
 * \brief Example code for Simple Open EtherCAT master running
 * a FSoE Master Application.
 *
 * (c)Andreas Karlsson 2019
 */

typedef struct sync_info
{
   int64 sync_cycles;
   int64 starttime_ref;
   int64 starttime_actual;
   int64 starttime_tmr_isr;
   int32 sync_period_us;
   int32 dc_offset;
   int32 in_sync;
   int32 listen_to_heartbeat;
} sync_info_t;
sync_info_t sync_me;

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

volatile int global_wkc;
int wait_for_in_sync = 1;

int ecatPdtmr;
SEM_ID ecatPdsem;

void toggle_in_sync(void)
{
   if (wait_for_in_sync)
      wait_for_in_sync = 0;
   else
      wait_for_in_sync = 1;
}

static char ifname[] = "gei";
static int device_index = 0;
static char IOmap[512];

static ec_slavet mio_ec_slaves[EC_MAXSLAVE];
/** number of slaves found on the network */
static int mio_slavecount;
/** slave group structure */
static ec_groupt ec_groups[EC_MAXGROUP];

/** cache for EEPROM read functions */
static uint8 esibuf[EC_MAXEEPBUF];
/** bitmap for filled cache buffer bytes */
static uint32 esimap[EC_MAXEEPBITMAP];
/** current slave for EEPROM cache buffer */
static ec_eringt ec_elist;
static ec_idxstackT ec_idxstack;
/** SyncManager Communication Type struct to store data of one slave */
static ec_SMcommtypet ec_SMcommtype;
/** PDO assign struct to store data of one slave */
static ec_PDOassignt ec_PDOassign;
/** PDO description struct to store data of one slave */
static ec_PDOdesct ec_PDOdesc;
/** buffer for EEPROM SM data */
static ec_eepromSMt ec_SM;
/** buffer for EEPROM FMMU data */
static ec_eepromFMMUt ec_FMMU;
/** Global variable TRUE if error available in error stack */
static boolean EcatError = FALSE;
int64 ec_DCtime;

static ecx_portt ecx_port;

static int pdo_thread_running = 0;

/* Make public to simply this sample code */
ecx_contextt mio_ctx = {
    &ecx_port,
    &mio_ec_slaves[0],
    &mio_slavecount,
    EC_MAXSLAVE,
    &ec_groups[0],
    EC_MAXGROUP,
    &esibuf[0],
    &esimap[0],
    0,
    &ec_elist,
    &ec_idxstack,
    &EcatError,
    0,
    0,
    &ec_DCtime,
    &ec_SMcommtype,
    &ec_PDOassign,
    &ec_PDOdesc,
    &ec_SM,
    &ec_FMMU,
    NULL,
};

/* Configure and activate DC slaves, this is application specific
 * this serves as an example.
 */
static int slave_dc_config(ecx_contextt *ctx, uint16 slave)
{
   if (slave == 1)
   {
      (void)ecx_dcsync0(&mio_ctx,
                        1,
                        TRUE,
                        sync_me.sync_period_us * 1000,
                        sync_me.dc_offset * 1000);
   }
   else
   {
      (void)ecx_dcsync0(&mio_ctx,
                        slave,
                        TRUE,
                        sync_me.sync_period_us * 1000,
                        sync_me.dc_offset * 1000);
   }
   return 0;
}

/* Wait for local diff in all slaves to reach a minimal value AND
 *  wait for the Master clock and DC clock to be synced.
 *  Times to wait are application estimates.
 */
static int wait_for_sync(void)
{
   int cnt = 0, dcsysdiff;

   /* Release the task to start now that the start time is set */
   (void)semBGive(ecatPdsem);
   taskDelay(5); /* let the PDO task start */
   printf("\n");
   /* Check for DC status for slaves only */
   do
   {
      (void)ecx_BRD(mio_ctx.port,
                    0x0000,
                    ECT_REG_DCSYSDIFF,
                    sizeof(dcsysdiff),
                    &dcsysdiff,
                    EC_TIMEOUTSAFE);

      if (cnt % 1000000)
      {
         printf("dc diff slave %12u\r", (dcsysdiff & 0x7FFFFFFF));
      }
      taskDelay(100);
      cnt++;
   } while (((dcsysdiff & 0x7FFFFFFF) > (0x1ff)) || wait_for_in_sync);

   printf("\nSync slaves function run %d times\n", cnt);
   appState = SYNC_MASTER;
   /* Let sync of master clock start */
   taskDelay(10);
   cnt = 0;
   /* Check for DC status for slaves only */
   do
   {
      (void)ecx_BRD(mio_ctx.port,
                    0x0000,
                    ECT_REG_DCSYSDIFF,
                    sizeof(dcsysdiff),
                    &dcsysdiff,
                    EC_TIMEOUTSAFE);

      if (cnt % 1000000)
      {
         printf("dc diff master %12u\r", (dcsysdiff & 0x7FFFFFFF));
      }
      taskDelay(100);
      cnt++;
   } while (((dcsysdiff & 0x7FFFFFFF) > (0x1fff)) || wait_for_in_sync);

   printf("\nSync master to slave dc ref function run %d times\n", cnt);

   return 0;
}

void mio_sim_ecatmaster(void)
{
   int i, expectedWKC;
   printf("Starting EtherCAT master\n");
   /* initialise SOEM, bind socket to ifname */
   if (ecx_init(&mio_ctx, ifname, device_index))
   {
      printf("ec_init on %s succeeded.\n", ifname);
      appState = CONFIG;
      /* find and auto-config slaves */
      if (ecx_config_init(&mio_ctx, FALSE) > 0)
      {
         /* configure DC options for every DC capable slave found in the list
          * Add config hook called by  ec_config_map.
          */
         ecx_configdc(&mio_ctx);

         for (i = 1; i <= mio_slavecount; i++)
         {
            printf("Slave %d, port delay %d\n", i, mio_ec_slaves[i].pdelay);
            if (mio_ec_slaves[i].hasdc > 0)
            {
               mio_ec_slaves[i].PO2SOconfigx = slave_dc_config;
            }
         }
         printf("%d slaves found and configured.\n", *mio_ctx.slavecount);
         ecx_config_map_group(&mio_ctx, &IOmap, 0);
         memset(IOmap, 0, sizeof(IOmap));
         /* Setup FSOE Network when EtherCAT slaves have been configured and mapped */
         safety_setup();

         printf("Slaves mapped, state to SAFE_OP.\n");
         /* wait for all slaves to reach SAFE_OP state */
         ecx_statecheck(&mio_ctx, 0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE * 4);

         appState = SYNC_SLAVES;

         /* read indevidual slave state and store in ec_slave[] */
         ecx_readstate(&mio_ctx);
         for (i = 1; i <= mio_slavecount; i++)
         {
            printf("Slave:%d Name:%s Output size:%3dbits Input size:%3dbits State:%2d delay:%d.%d\n",
                   i,
                   mio_ec_slaves[i].name,
                   mio_ec_slaves[i].Obits,
                   mio_ec_slaves[i].Ibits,
                   mio_ec_slaves[i].state,
                   (int)mio_ec_slaves[i].pdelay,
                   mio_ec_slaves[i].hasdc);
            printf("         Out:%8.8x,%4d In:%8.8x,%4d\n",
                   (int)mio_ec_slaves[i].outputs,
                   (int)mio_ec_slaves[i].Obytes,
                   (int)mio_ec_slaves[i].inputs,
                   (int)mio_ec_slaves[i].Ibytes);
         }

         expectedWKC = (mio_ctx.grouplist[0].outputsWKC * 2) + mio_ctx.grouplist[0].inputsWKC;
         printf("Calculated workcounter %d\n", expectedWKC);

         /* Hang here to sync */
         wait_for_sync();

         /* Go to OP */
         printf("Request operational state for all slaves\n");
         mio_ec_slaves[0].state = EC_STATE_OPERATIONAL;
         /* request OP state for all slaves */
         ecx_writestate(&mio_ctx, 0);
         /* wait for all slaves to reach OP state */
         ecx_statecheck(&mio_ctx, 0, EC_STATE_OPERATIONAL, EC_TIMEOUTSTATE);

         if (mio_ec_slaves[0].state == EC_STATE_OPERATIONAL)
         {
            appState = RUN;
            printf("Operational state reached for all slaves.\n");
            while (pdo_thread_running == 1)
            {
               taskDelay(10000);
               if (global_wkc < expectedWKC)
               {
                  printf("WKC : %d < expectedWKC : %d\n", global_wkc, expectedWKC);
               }
               printf("\r");
            }
         }
         else
         {
            printf("Not all slaves reached operational state.\n");
            ecx_readstate(&mio_ctx);
            for (i = 1; i <= *mio_ctx.slavecount; i++)
            {
               if (mio_ctx.slavelist[i].state != EC_STATE_OPERATIONAL)
               {
                  printf("Slave %d State=0x%2.2x StatusCode=0x%4.4x : %s\n",
                         i, mio_ctx.slavelist[i].state,
                         mio_ctx.slavelist[i].ALstatuscode,
                         ec_ALstatuscode2string(mio_ctx.slavelist[i].ALstatuscode));
               }
            }
         }
         printf("\nRequest init state for all slaves\n");
         mio_ctx.slavelist[0].state = EC_STATE_INIT;
         /* request INIT state for all slaves */
         ecx_writestate(&mio_ctx, 0);
      }
      else
      {
         printf("No slaves found!\n");
      }
      printf("End simple test, close socket\n");
      /* stop SOEM, close socket */
      ecx_close(&mio_ctx);
   }
   else
   {
      printf("No socket connection on %s\nExcecute as root\n", ifname);
   }
}

static void heartbeatIsr(int index)
{
   /* This is the correct timer index */
   if (index == 3)
   {
      if (sync_me.listen_to_heartbeat == 0)
         return;
      if (ecatPdsem != SEM_ID_NULL)
      {
         sync_me.sync_cycles++;
         /* Release the task */
         (void)semBGive(ecatPdsem);
      }
   }
}

void ecatthread(int cycle)
{
   int64 current_sync_cycle;
   int64 external_ref_cock;
   int64 next_dc_sync0_puls;
   ec_timet mastertime;
   int64 mastertime_us;

   /* wait for DC activated */
   semTake(ecatPdsem, WAIT_FOREVER);

   /* Start timer now + 1 cycles in even cycles */
   mastertime = osal_current_time();
   mastertime_us = (((uint64)mastertime.sec * 1000000) + (uint64)mastertime.usec);
   sync_me.starttime_ref = ((mastertime_us + sync_me.sync_period_us) / sync_me.sync_period_us) * sync_me.sync_period_us + sync_me.sync_period_us;

   /* wait for just before DC starts */
   do
   {
      mastertime = osal_current_time();
      sync_me.starttime_actual = (((uint64)mastertime.sec * 1000000) + (uint64)mastertime.usec);
   } while ((sync_me.starttime_actual + 10) < sync_me.starttime_ref);

   printf("pd timer started at %llu, start ref %llu\n",
          sync_me.starttime_actual,
          sync_me.starttime_ref);

   sync_me.listen_to_heartbeat = 1;

   while (pdo_thread_running)
   {
      /* wait to cycle start */
      if (appState > SYNC_SLAVES)
         semTake(ecatPdsem, WAIT_FOREVER);
      else
         semTake(ecatPdsem, NO_WAIT);

      current_sync_cycle = sync_me.sync_cycles;

      if (appState > CONFIG)
      {
         if (appState > SYNC_SLAVES)
         {
            /* Set ref time bound to FPGA heart beat */
            external_ref_cock = ((current_sync_cycle * sync_me.sync_period_us) + sync_me.starttime_ref) * NSEC_PER_USEC;
            (void)ecx_FPWR(mio_ctx.port,
                           mio_ctx.slavelist[mio_ctx.grouplist[0].DCnext].configadr,
                           ECT_REG_DCSYSTIME,
                           sizeof(external_ref_cock),
                           &external_ref_cock,
                           EC_TIMEOUTRET);
         }

         ecx_send_processdata(&mio_ctx);
         global_wkc = ecx_receive_processdata(&mio_ctx, EC_TIMEOUTRET);

         if (appState > SYNC_MASTER)
         {
            safety_app();
         }
      }
   }
}
