/* esp.h:  Defines and structures for the Sparc ESP (Enhanced SCSI
 *         Processor) driver under Linux.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_ESP_H
#define _SPARC_ESP_H

/* For dvma controller register definitions. */
#include <asm/dma.h>

/* The ESP SCSI controllers have their register sets in three
 * "classes":
 *
 * 1) Registers which are both read and write.
 * 2) Registers which are read only.
 * 3) Registers which are write only.
 *
 * Yet, they all live within the same IO space.
 */

/* All the ESP registers are one byte each and are accessed longwords
 * apart with a big-endian ordering to the bytes.
 */

struct Sparc_ESP_regs {
                                /* Access    Description              Offset */
    volatile unchar esp_tclow;  /* rw  Low bits of the transfer count 0x00   */
                                unchar tlpad1[3];
    volatile unchar esp_tcmed;  /* rw  Mid bits of the transfer count 0x04   */
                                unchar fdpad[3];
    volatile unchar esp_fdata;  /* rw  FIFO data bits                 0x08   */
                                unchar cbpad[3];
    volatile unchar esp_cmd;    /* rw  SCSI command bits              0x0c   */
                                unchar stpad[3];
    volatile unchar esp_status; /* ro  ESP status register            0x10   */
#define esp_busid   esp_status  /* wo  Bus ID for select/reselect     0x10   */
                                unchar irqpd[3];
    volatile unchar esp_intrpt; /* ro  Kind of interrupt              0x14   */
#define esp_timeo   esp_intrpt  /* wo  Timeout value for select/resel 0x14   */
                                unchar sspad[3];
    volatile unchar esp_sstep;  /* ro  Sequence step register         0x18   */
#define esp_stp     esp_sstep   /* wo  Transfer period per sync       0x18   */
                                unchar ffpad[3];
    volatile unchar esp_fflags; /* ro  Bits of current FIFO info      0x1c   */
#define esp_soff    esp_fflags  /* wo  Sync offset                    0x1c   */
                                unchar cf1pd[3];
    volatile unchar esp_cfg1;   /* rw  First configuration register   0x20   */
                                unchar cfpad[3];
    volatile unchar esp_cfact;  /* wo  Clock conversion factor        0x24   */
                                unchar ctpad[3];
    volatile unchar esp_ctest;  /* wo  Chip test register             0x28   */
                                unchar cf2pd[3];
    volatile unchar esp_cfg2;   /* rw  Second configuration register  0x2c   */
                                unchar cf3pd[3];

    /* The following is only found on the 53C9X series SCSI chips */
    volatile unchar esp_cfg3;   /* rw  Third configuration register   0x30  */
                                unchar thpd[7];

    /* The following is found on all chips except the NCR53C90 (ESP100) */
    volatile unchar esp_tchi;   /* rw  High bits of transfer count    0x38  */
#define esp_uid     esp_tchi    /* ro  Unique ID code                 0x38  */
                                unchar fgpad[3];
    volatile unchar esp_fgrnd;  /* rw  Data base for fifo             0x3c  */
};

/* Various revisions of the ESP board. */
enum esp_rev {
  esp100     = 0x00,  /* NCR53C90  */
  esp100a    = 0x01,  /* NCR53C90A */
  esp236     = 0x02,
  fas236     = 0x03,
  fas100a    = 0x04,
  fast       = 0x05,
  espunknown = 0x06
};

/* We get one of these for each ESP probed. */
struct Sparc_ESP {
  struct Sparc_ESP *next;           /* Next ESP on probed or NULL */
  struct Sparc_ESP_regs *eregs;           /* All esp registers */
  struct Linux_SBus_DMA *dma;             /* Who I do transfers with. */
  struct sparc_dma_registers *dregs;      /* And his registers. */
  struct Scsi_Host *ehost;                /* Backpointer to SCSI Host */

  struct linux_sbus_device *edev;         /* Pointer to SBus entry */
  char prom_name[64];                     /* Name of ESP device from prom */
  int prom_node;                          /* Prom node where ESP found */
  int esp_id;                             /* Same as esphost->host_id */

  /* ESP Configuration Registers */
  unsigned char config1;                  /* Copy of the 1st config register */
  unsigned char config2;                  /* Copy of the 2nd config register */
  unsigned char config3[8];               /* Copy of the 3rd config register */

  /* The current command we are sending to the ESP chip.  This esp_command
   * ptr needs to be mapped in DVMA area so we can send commands and read
   * from the ESP fifo without burning precious CPU cycles.  Programmed I/O
   * sucks when we have the DVMA to do it for us.
   */
  volatile unchar *esp_command;           /* Location of command */
  int esp_clen;                           /* Length of this command */

  /* To hold onto the dvma buffer ptr. */
  char *dvma_hold;

  /* The following are used to determine the cause of an IRQ. Upon every
   * IRQ entry we synchronize these with the hardware registers.
   */
  unchar ireg;                            /* Copy of ESP interrupt register */
  unchar sreg;                            /* Same for ESP status register */
  unchar seqreg;                          /* The ESP sequence register */

  /* Clock periods, frequencies, synchronization, etc. */
  unsigned int cfreq;                    /* Clock frequency in HZ */
  unsigned int cfact;                    /* Clock conversion factor */
  unsigned int ccycle;                   /* One ESP clock cycle */
  unsigned int ctick;                    /* One ESP clock time */
  unsigned int sync_defp;                /* Default negotiation period */

  /* Misc. info about this ESP */
  enum esp_rev erev;                      /* ESP revision */
  int irq;                                /* SBus IRQ for this ESP */
  int scsi_id;                            /* Who am I as initiator? */
  int scsi_id_mask;                       /* Bitmask of 'me'. */
  int diff;                               /* Differential SCSI? */
  int bursts;                             /* Burst sizes our DVMA supports */

  /* Our command queues, only one cmd lives in the current_SC queue. */
  Scsi_Cmnd *issue_SC;           /* Commands to be issued */
  Scsi_Cmnd *current_SC;         /* Who is currently working the bus */
  Scsi_Cmnd *disconnected_SC;    /* Commands disconnected from the bus */

#ifdef THREADED_ESP_DRIVER
  Scsi_Cmnd *eatme_SC;           /* Cmds waiting for esp thread to process. */
#endif

  /* Abortion status */
  int aborting, abortion_complete, abort_result;
};

/* Bitfield meanings for the above registers. */

/* ESP config reg 1, read-write, found on all ESP chips */
#define ESP_CONFIG1_ID        0x07             /* My BUS ID bits */
#define ESP_CONFIG1_CHTEST    0x08             /* Enable ESP chip tests */
#define ESP_CONFIG1_PENABLE   0x10             /* Enable parity checks */
#define ESP_CONFIG1_PARTEST   0x20             /* Parity test mode enabled? */
#define ESP_CONFIG1_SRRDISAB  0x40             /* Disable SCSI reset reports */
#define ESP_CONFIG1_SLCABLE   0x80             /* Enable slow cable mode */

/* ESP config reg 2, read-write, found only on esp100a+esp200+esp236 chips */
#define ESP_CONFIG2_DMAPARITY 0x01             /* Parity DMA err (200,236) */
#define ESP_CONFIG2_REGPARITY 0x02             /* Parity reg err (200,236) */
#define ESP_CONFIG2_BADPARITY 0x04             /* Bad parity target abort  */
#define ESP_CONFIG2_SCSI2ENAB 0x08             /* Enable SCSI-2 features   */
#define ESP_CONFIG2_HI        0x10             /* High Impedance DREQ ???  */
#define ESP_CONFIG2_BCM       0x20             /* Enable byte-ctrl (236)   */
#define ESP_CONFIG2_FENAB     0x40             /* Enable features (fas100,esp216)      */
#define ESP_CONFIG2_SPL       0x40             /* Enable status-phase latch (esp236)   */
#define ESP_CONFIG2_MAGIC     0xe0             /* Invalid bits... */

/* ESP config register 3 read-write, found only esp236+fas236+fas100a chips */
#define ESP_CONFIG3_FCLOCK    0x01             /* FAST SCSI clock rate (esp100a)     */
#define ESP_CONFIG3_TEM       0x01             /* Enable thresh-8 mode (esp/fas236)  */
#define ESP_CONFIG3_FAST      0x02             /* Enable FAST SCSI     (esp100a)     */
#define ESP_CONFIG3_ADMA      0x02             /* Enable alternate-dma (esp/fas236)  */
#define ESP_CONFIG3_TENB      0x04             /* group2 SCSI2 support (esp100a)     */
#define ESP_CONFIG3_SRB       0x04             /* Save residual byte   (esp/fas236)  */
#define ESP_CONFIG3_TMS       0x08             /* Three-byte msg's ok  (esp100a)     */
#define ESP_CONFIG3_FCLK      0x08             /* Fast SCSI clock rate (esp/fas236)  */
#define ESP_CONFIG3_IDMSG     0x10             /* ID message checking  (esp100a)     */
#define ESP_CONFIG3_FSCSI     0x10             /* Enable FAST SCSI     (esp/fas236)  */
#define ESP_CONFIG3_GTM       0x20             /* group2 SCSI2 support (esp/fas236)  */
#define ESP_CONFIG3_TBMS      0x40             /* Three-byte msg's ok  (esp/fas236)  */
#define ESP_CONFIG3_IMS       0x80             /* ID msg chk'ng        (esp/fas236)  */

/* ESP command register read-write */
/* Group 1 commands:  These may be sent at any point in time to the ESP
 *                    chip.  None of them can generate interrupts 'cept
 *                    the "SCSI bus reset" command if you have not disabled
 *                    SCSI reset interrupts in the config1 ESP register.
 */
#define ESP_CMD_NULL          0x00             /* Null command, ie. a nop */
#define ESP_CMD_FLUSH         0x01             /* FIFO Flush */
#define ESP_CMD_RC            0x02             /* Chip reset */
#define ESP_CMD_RS            0x03             /* SCSI bus reset */

/* Group 2 commands:  ESP must be an initiator and connected to a target
 *                    for these commands to work.
 */
#define ESP_CMD_TI            0x10             /* Transfer Information */
#define ESP_CMD_ICCSEQ        0x11             /* Initiator cmd complete sequence */
#define ESP_CMD_MOK           0x12             /* Message okie-dokie */
#define ESP_CMD_TPAD          0x18             /* Transfer Pad */
#define ESP_CMD_SATN          0x1a             /* Set ATN */
#define ESP_CMD_RATN          0x1b             /* De-assert ATN */

/* Group 3 commands:  ESP must be in the MSGOUT or MSGIN state and be connected
 *                    to a target as the initiator for these commands to work.
 */
#define ESP_CMD_SMSG          0x20             /* Send message */
#define ESP_CMD_SSTAT         0x21             /* Send status */
#define ESP_CMD_SDATA         0x22             /* Send data */
#define ESP_CMD_DSEQ          0x23             /* Discontinue Sequence */
#define ESP_CMD_TSEQ          0x24             /* Terminate Sequence */
#define ESP_CMD_TCCSEQ        0x25             /* Target cmd cmplt sequence */
#define ESP_CMD_DCNCT         0x27             /* Disconnect */
#define ESP_CMD_RMSG          0x28             /* Receive Message */
#define ESP_CMD_RCMD          0x29             /* Receive Command */
#define ESP_CMD_RDATA         0x2a             /* Receive Data */
#define ESP_CMD_RCSEQ         0x2b             /* Receive cmd sequence */

/* Group 4 commands:  The ESP must be in the disconnected state and must
 *                    not be connected to any targets as initiator for
 *                    these commands to work.
 */
#define ESP_CMD_RSEL          0x40             /* Reselect */
#define ESP_CMD_SEL           0x41             /* Select w/o ATN */
#define ESP_CMD_SELA          0x42             /* Select w/ATN */
#define ESP_CMD_SELAS         0x43             /* Select w/ATN & STOP */
#define ESP_CMD_ESEL          0x44             /* Enable selection */
#define ESP_CMD_DSEL          0x45             /* Disable selections */
#define ESP_CMD_SA3           0x46             /* Select w/ATN3 */
#define ESP_CMD_RSEL3         0x47             /* Reselect3 */

/* This bit enables the ESP's DMA on the SBus */
#define ESP_CMD_DMA           0x80             /* Do DMA? */


/* ESP status register read-only */
#define ESP_STAT_PIO          0x01             /* IO phase bit */
#define ESP_STAT_PCD          0x02             /* CD phase bit */
#define ESP_STAT_PMSG         0x04             /* MSG phase bit */
#define ESP_STAT_PMASK        0x07             /* Mask of phase bits */
#define ESP_STAT_TDONE        0x08             /* Transfer Completed */
#define ESP_STAT_TCNT         0x10             /* Transfer Counter Is Zero */
#define ESP_STAT_PERR         0x20             /* Parity error */
#define ESP_STAT_SPAM         0x40             /* Real bad error */
/* This indicates the 'interrupt pending' condition on esp236, it is a reserved
 * bit on other revs of the ESP.
 */
#define ESP_STAT_INTR         0x80             /* Interrupt */

/* The status register can be masked with ESP_STAT_PMASK and compared
 * with the following values to determine the current phase the ESP
 * (at least thinks it) is in.  For our pusposes we also add our own
 * software 'done' bit for our phase management engine.
 */
#define ESP_DOP   (0)                                       /* Data Out  */
#define ESP_DIP   (ESP_STAT_PIO)                            /* Data In   */
#define ESP_CMDP  (ESP_STAT_PCD)                            /* Command   */
#define ESP_STATP (ESP_STAT_PCD|ESP_STAT_PIO)               /* Status    */
#define ESP_MOP   (ESP_STAT_PMSG|ESP_STAT_PCD)              /* Message Out */
#define ESP_MIP   (ESP_STAT_PMSG|ESP_STAT_PCD|ESP_STAT_PIO) /* Message In */

/* ESP interrupt register read-only */
#define ESP_INTR_S            0x01             /* Select w/o ATN */
#define ESP_INTR_SATN         0x02             /* Select w/ATN */
#define ESP_INTR_RSEL         0x04             /* Reselected */
#define ESP_INTR_FDONE        0x08             /* Function done */
#define ESP_INTR_BSERV        0x10             /* Bus service */
#define ESP_INTR_DC           0x20             /* Disconnect */
#define ESP_INTR_IC           0x40             /* Illegal command given */
#define ESP_INTR_SR           0x80             /* SCSI bus reset detected */

/* Interrupt status macros */
#define ESP_SRESET_IRQ(esp)  ((esp)->intreg & (ESP_INTR_SR))
#define ESP_ILLCMD_IRQ(esp)  ((esp)->intreg & (ESP_INTR_IC))
#define ESP_SELECT_WITH_ATN_IRQ(esp)     ((esp)->intreg & (ESP_INTR_SATN))
#define ESP_SELECT_WITHOUT_ATN_IRQ(esp)  ((esp)->intreg & (ESP_INTR_S))
#define ESP_SELECTION_IRQ(esp)  ((ESP_SELECT_WITH_ATN_IRQ(esp)) ||         \
				 (ESP_SELECT_WITHOUT_ATN_IRQ(esp)))
#define ESP_RESELECTION_IRQ(esp)         ((esp)->intreg & (ESP_INTR_RSEL))

/* ESP sequence step register read-only */
#define ESP_STEP_VBITS        0x07             /* Valid bits */
#define ESP_STEP_ASEL         0x00             /* Selection&Arbitrate cmplt */
#define ESP_STEP_SID          0x01             /* One msg byte sent */
#define ESP_STEP_NCMD         0x02             /* Was not in command phase */
#define ESP_STEP_PPC          0x03             /* Early phase chg caused cmnd
                                                * bytes to be lost
                                                */
#define ESP_STEP_FINI         0x04             /* Command was sent ok */

/* ESP chip-test register read-write */
#define ESP_TEST_TARG         0x01             /* Target test mode */
#define ESP_TEST_INI          0x02             /* Initiator test mode */
#define ESP_TEST_TS           0x04             /* Tristate test mode */

/* ESP unique ID register read-only, found on fas236+fas100a only */
#define ESP_UID_F100A         0x00             /* ESP FAS100A  */
#define ESP_UID_F236          0x02             /* ESP FAS236   */
#define ESP_UID_REV           0x07             /* ESP revision */
#define ESP_UID_FAM           0xf8             /* ESP family   */

/* ESP fifo flags register read-only */
/* Note that the following implies a 16 byte FIFO on the ESP. */
#define ESP_FF_FBYTES         0x1f             /* Num bytes in FIFO */
#define ESP_FF_SSTEP          0xe0             /* Sequence step */

/* ESP clock conversion factor register write-only */
#define ESP_CCF_F0            0x00             /* 35.01MHz - 40MHz */
#define ESP_CCF_NEVER         0x01             /* Set it to this and die */
#define ESP_CCF_F2            0x02             /* 10MHz */
#define ESP_CCF_F3            0x03             /* 10.01MHz - 15MHz */
#define ESP_CCF_F4            0x04             /* 15.01MHz - 20MHz */
#define ESP_CCF_F5            0x05             /* 20.01MHz - 25MHz */
#define ESP_CCF_F6            0x06             /* 25.01MHz - 30MHz */
#define ESP_CCF_F7            0x07             /* 30.01MHz - 35MHz */

extern int esp_detect(struct SHT *);
extern const char *esp_info(struct Scsi_Host *);
extern int esp_queue(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
extern int esp_command(Scsi_Cmnd *);
extern int esp_abort(Scsi_Cmnd *);
extern int esp_reset(Scsi_Cmnd *, unsigned int);

extern struct proc_dir_entry proc_scsi_esp;

#define SCSI_SPARC_ESP {                                                               \
/* struct SHT *next */                                         NULL,                   \
/* long *usage_count */                                        NULL,                   \
/* struct proc_dir_entry *proc_dir */                          &proc_scsi_esp,         \
/* int (*proc_info)(char *, char **, off_t, int, int, int) */  NULL,                   \
/* const char *name */                                         "Sun ESP 100/100a/200", \
/* int detect(struct SHT *) */                                 esp_detect,             \
/* int release(struct Scsi_Host *) */                          NULL,                   \
/* const char *info(struct Scsi_Host *) */                     esp_info,               \
/* int command(Scsi_Cmnd *) */                                 esp_command,            \
/* int queuecommand(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *)) */ esp_queue,              \
/* int abort(Scsi_Cmnd *) */                                   esp_abort,              \
/* int reset(Scsi_Cmnd *, int) */                              esp_reset,              \
/* int slave_attach(int, int) */                               NULL,                   \
/* int bios_param(Disk *, kdev_t, int[]) */                    NULL,                   \
/* int can_queue */                                            10,                     \
/* int this_id */                                              7,                      \
/* short unsigned int sg_tablesize */                          SG_ALL,                 \
/* short cmd_per_lun */                                        1,                      \
/* unsigned char present */                                    0,                      \
/* unsigned unchecked_isa_dma:1 */                             0,                      \
/* unsigned use_clustering:1 */                                DISABLE_CLUSTERING, }

/* For our interrupt engine. */
#define for_each_esp(esp) \
        for((esp) = espchain; (esp); (esp) = (esp)->next)

#endif /* !(_SPARC_ESP_H) */
