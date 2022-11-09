#if !(defined _L1_RPC_H_)
#define _L1_RPC_H_

/******** MAXPOWER **********/
typedef struct {
	unsigned char rat;	/* WB = 1,GSM = 2 */
	unsigned char band;	/* WB [1-10],GSM [1800 -> 1,1900 -> 2, 900 -> 3, 850 -> 6] */
	unsigned short arfcn;
	signed short txPower;	/*Q0. MAX_POWER == 0xFFFF */
	char txOn;		/*If TRUE -> Transmit according to the above. If FALSE -> stop transmitting, and Terminate Active_RAT. */
	char pad;
} plAmMaxPowerAtCommand_ts;

/************************************ Start UBLOX Definitions **********************************************************************/

/********* UTEST *************/
typedef enum {
	UTEST_MODE_DISABLED = 0,
	UTEST_MODE_ENABLED = 1,
	UTEST_MODE_RX = 1,
	UTEST_MODE_TX = 2,
	UTEST_MODE_MAX = 3
} plAmUserTestAtCommandMode_e;

typedef struct {
	unsigned char mode;	/*0-diable, 1-rx, 2-tx */
	unsigned char rat;	/* WB = 1,GSM = 2 */
	unsigned char band;	/* WB [1-10],GSM [1800 -> 1,1900 -> 2, 900 -> 3, 850 -> 6] */
	unsigned char antenna_diversity;	/*for 3G only: 0-off, 1-on */
	unsigned short arfcn;
	signed short txPower;	/*db in Q0 [-56,24] */
	unsigned char trainingSeq;	/*For 2G only [0:7] */
	unsigned char modulationMode;	/*for 2G only: 1-GMSK, 2-8PSK */
	unsigned char pad[2];
	unsigned long interval;	/*in ms */
} plAmUserTestAtCommand_ts;

typedef struct {
	signed char rx_min;	/*db Q0 */
	signed char rx_avg;	/*db Q0 */
	signed char rx_max;	/*db Q0 */
	unsigned char returnValue;
} plAmUserTestAtCommandRxInd_ts;

/*GSM ARFCN BAND limits define */
#define ARFCN_GSM900_MIN	(0L)
#define ARFCN_GSM900_MAX	(124L)
#define ARFCN_GSM850_MIN	(128L)
#define ARFCN_GSM850_MAX	(251L)
#define ARFCN_DCS1800_MIN	(512L)
#define ARFCN_DCS1800_MAX	(885L)
#define ARFCN_PCS1900_MIN	(33280L)	/*512 */
#define ARFCN_PCS1900_MAX	(33578L)	/*810 */
#define ARFCN_EGSM900_MIN	(975L)
#define ARFCN_EGSM900_MAX	(1023L)

/*UMTS ARFCN BAND limits define */
#define ARFCN_BAND_IV_MIN	(1537L)
#define ARFCN_BAND_IV_MAX	(1738L)
#define ARFCN_BAND_VIII_MIN	(2937L)
#define ARFCN_BAND_VIII_MAX	(3088L)
#define ARFCN_BAND_V_MIN	(4357L)
#define ARFCN_BAND_V_MAX	(4458L)
#define ARFCN_BAND_II_MIN	(9662L)
#define ARFCN_BAND_II_MAX	(9938L)
#define ARFCN_BAND_I_MIN	(10562L)
#define ARFCN_BAND_I_MAX	(10838L)

/*GSM RX ARFCN BAND limits define */
#define TX_ARFCN_GSM900_MIN		(0L)
#define TX_ARFCN_GSM900_MAX		(124L)
#define TX_ARFCN_GSM850_MIN		(128L)
#define TX_ARFCN_GSM850_MAX		(251L)
#define TX_ARFCN_DCS1800_MIN	(512L)
#define TX_ARFCN_DCS1800_MAX	(885L)
#define TX_ARFCN_PCS1900_MIN	(33280L)	/*512 */
#define TX_ARFCN_PCS1900_MAX	(33578L)	/*810 */
#define TX_ARFCN_EGSM900_MIN	(975L)
#define TX_ARFCN_EGSM900_MAX	(1023L)

/*UMTS ARFCN BAND limits define */
#define TX_ARFCN_BAND_IV_MIN	(1312L)
#define TX_ARFCN_BAND_IV_MAX	(1513L)
#define TX_ARFCN_BAND_VIII_MIN	(2712L)
#define TX_ARFCN_BAND_VIII_MAX	(2863L)
#define TX_ARFCN_BAND_V_MIN		(4321L)
#define TX_ARFCN_BAND_V_MAX		(4233L)
#define TX_ARFCN_BAND_II_MIN	(9262L)
#define TX_ARFCN_BAND_II_MAX	(9538L)
#define TX_ARFCN_BAND_I_MIN		(9612L)
#define TX_ARFCN_BAND_I_MAX		(9888L)

#define UTEST_RAT_NORAT	(0)
#define UTEST_RAT_UMTS	(1)
#define UTEST_RAT_GSM	(2)

typedef enum {
	RF_CALIBRATION_BAND_MIN = 0,
	RF_CALIBRATION_BAND_2_GHZ = 1,	/* band 1 */
	RF_CALIBRATION_BAND_1900_MHZ = 2,	/* band 2 */
	RF_CALIBRATION_BAND_1800_MHZ = 3,	/* band 3 */
	RF_CALIBRATION_BAND_2100_MHZ = 4,	/* band 4 */
	RF_CALIBRATION_BAND_800_MHZ = 5,	/* band 5 */
	RF_CALIBRATION_BAND_REDUCED_800_MHZ = 6,	/* band 6 */
	RF_CALIBRATION_BAND_2600_MHZ = 7,	/* band 7 */
	RF_CALIBRATION_BAND_900_MHZ = 8,	/* band 8 */
	RF_CALIBRATION_BAND_REDUCED_1800_MHZ = 9,	/* band 9 */
	RF_CALIBRATION_BAND_EXPANDED_2100_MHZ = 10,	/* band 10 */
	RF_CALIBRATION_BAND_NUMBER,
	RF_CALIBRATION_BAND_IS_NOT_DEFINED = 0xffff
} plCalibUarfcnBandsLimits_te;

/************************************ Start NST Definitions **********************************************************************/
typedef struct {
	unsigned short UARFCN;
	unsigned short dpchScrCode;
	unsigned char dpchChanCode;
	INT8 MaxTxPower;
	unsigned char pad[2];

} L1nstWBSyncAtCommand_ts;

#define PGSM_BAND_BIT           0x0001	/* (1 << GSM_BAND)  */
#define DCS_BAND_BIT            0x0002	/* (1 << DCS_BAND)  */
#define PCS_BAND_BIT            0x0004	/* (1 << PCS_BAND)  */
#define EGSM_EXTRA_BAND_BIT     0x0008	/* (1 << EGSM_BAND) */
#define EGSM_BAND_BITS          (PGSM_BAND_BIT + EGSM_EXTRA_BAND_BIT)
#define GSM450_EXTRA_BAND_BIT   0x0010
#define GSM480_EXTRA_BAND_BIT   0x0020
#define GSM400_EXTRA_BAND_BITS  (GSM450_EXTRA_BAND_BIT + GSM480_EXTRA_BAND_BIT)
#define GSM850_BAND_BIT         0x0040
#define GSM750_BAND_BIT         0x0080
#define UMTS_BAND_BIT           0x0100
#define BAND_LOCK_BIT           0x0200
typedef enum BandModeTag {
	ZERO_BAND_MODE = 0x0000,
	PGSM_MODE = PGSM_BAND_BIT,																										     /**< Autoband enabled */
	EGSM_MODE = EGSM_BAND_BITS,																										    /**< Autoband enabled */
	DCS_MODE = DCS_BAND_BIT,																											 /**< Autoband enabled */
	PGSM_DCS_MODE = PGSM_BAND_BIT + DCS_BAND_BIT,																				       /**< Autoband enabled */
	EGSM_DCS_MODE = EGSM_BAND_BITS + DCS_BAND_BIT,																				      /**< Autoband enabled */
	PCS_MODE = PCS_BAND_BIT,																											 /**< Autoband enabled */
	PGSM_PCS_MODE = PGSM_BAND_BIT + PCS_BAND_BIT,																				       /**< Autoband enabled */
	EGSM_PCS_MODE = EGSM_BAND_BITS + PCS_BAND_BIT,																				      /**< Autoband enabled */
	PGSM_PCS_MODE_LOCK = PGSM_BAND_BIT + PCS_BAND_BIT + BAND_LOCK_BIT,												    /**< LOCK the MS to PGSM/PCS mode. Autoband DISABLED. For use in testing and 900/1900 countries only */
	EGSM_PCS_MODE_LOCK = EGSM_BAND_BITS + PCS_BAND_BIT + BAND_LOCK_BIT,												   /**< LOCK the MS to EGSM/PCS mode. Autoband DISABLED. For use in testing and 900/1900 countries only */
	EGSM_DCS_MODE_LOCK = EGSM_BAND_BITS + DCS_BAND_BIT + BAND_LOCK_BIT,												   /**< LOCK the MS to EGSM/DCS mode. Autoband DISABLED. For use in testing and 900/1800 countries only */
	DCS_MODE_LOCK = DCS_BAND_BIT + BAND_LOCK_BIT,																			    /**< LOCK the MS to DCS mode. Autoband DISABLED. For use in testing only */
	PCS_MODE_LOCK = PCS_BAND_BIT + BAND_LOCK_BIT,																			    /**< LOCK the MS to PCS mode. Autoband DISABLED. For use in testing only */
	EGSM_MODE_LOCK = EGSM_BAND_BITS + BAND_LOCK_BIT,																			    /**< LOCK the MS to EGSM mode. Autoband DISABLED. For use in testing only */
	GSM850_MODE = GSM850_BAND_BIT,																										   /**< Autoband enabled */
	GSM850_PCS_MODE = GSM850_BAND_BIT + PCS_BAND_BIT,																		 /**< Autoband enabled */
	GSM850_PCS_MODE_LOCK = GSM850_BAND_BIT + PCS_BAND_BIT + BAND_LOCK_BIT,												  /**< LOCK the MS to GSM850/PCS1900 mode. */
	GSM850_PCS_EGSM_DCS_MODE =
	    GSM850_BAND_BIT + PCS_BAND_BIT + EGSM_DCS_MODE,
	GSM850_DCS_MODE = GSM850_BAND_BIT + DCS_BAND_BIT,																		 /**< Autoband enabled */
	GSM850_MODE_LOCK = GSM850_BAND_BIT + BAND_LOCK_BIT,																   /**< LOCK the MS to GSM850 mode. Autoband DISABLED. For use in testing only */
	GSM850_DCS_MODE_LOCK = GSM850_BAND_BIT + DCS_BAND_BIT + BAND_LOCK_BIT,										      /**< LOCK the MS to GSM850/DCS1800 mode. Autoband DISABLED. For use in testing and 850/1800 countries only */
	GSM850_GSM450_MODE = GSM850_BAND_BIT + GSM450_EXTRA_BAND_BIT,
	GSM850_GSM450_PCS_MODE = GSM850_BAND_BIT + GSM450_EXTRA_BAND_BIT + PCS_BAND_BIT,	/* Autoband enabled */
	GSM850_GSM480_MODE = GSM850_BAND_BIT + GSM480_EXTRA_BAND_BIT,	/* Autoband enabled */
	GSM850_GSM480_PCS_MODE = GSM850_BAND_BIT + GSM480_EXTRA_BAND_BIT + PCS_BAND_BIT,	/* Autoband enabled */
	GSM850_GSM400_MODE = GSM850_BAND_BIT + GSM450_EXTRA_BAND_BIT + GSM480_EXTRA_BAND_BIT,	/* Autoband enabled */
	GSM850_GSM400_PCS_MODE = GSM850_BAND_BIT + GSM450_EXTRA_BAND_BIT + GSM480_EXTRA_BAND_BIT + PCS_BAND_BIT,	/* Autoband enabled */
	PGSM450_MODE = PGSM_BAND_BIT + GSM450_EXTRA_BAND_BIT,	/* Autoband enabled */
	EGSM450_MODE = EGSM_BAND_BITS + GSM450_EXTRA_BAND_BIT,	/* Autoband enabled */
	PGSM450_DCS_MODE = PGSM_BAND_BIT + GSM450_EXTRA_BAND_BIT + DCS_BAND_BIT,	/* Autoband enabled */
	EGSM450_DCS_MODE = EGSM_BAND_BITS + GSM450_EXTRA_BAND_BIT + DCS_BAND_BIT,	/* Autoband enabled */
	PGSM450_PCS_MODE = PGSM_BAND_BIT + GSM450_EXTRA_BAND_BIT + PCS_BAND_BIT,	/* Autoband enabled */
	EGSM450_PCS_MODE = EGSM_BAND_BITS + GSM450_EXTRA_BAND_BIT + PCS_BAND_BIT,	/* Autoband enabled */
	PGSM480_MODE = PGSM_BAND_BIT + GSM480_EXTRA_BAND_BIT,	/* Autoband enabled */
	EGSM480_MODE = EGSM_BAND_BITS + GSM480_EXTRA_BAND_BIT,	/* Autoband enabled */
	PGSM480_DCS_MODE = PGSM_BAND_BIT + GSM480_EXTRA_BAND_BIT + DCS_BAND_BIT,	/* Autoband enabled */
	EGSM480_DCS_MODE = EGSM_BAND_BITS + GSM480_EXTRA_BAND_BIT + DCS_BAND_BIT,	/* Autoband enabled */
	PGSM480_PCS_MODE = PGSM_BAND_BIT + GSM480_EXTRA_BAND_BIT + PCS_BAND_BIT,	/* Autoband enabled */
	EGSM480_PCS_MODE = EGSM_BAND_BITS + GSM480_EXTRA_BAND_BIT + PCS_BAND_BIT,	/* Autoband enabled */
	PGSM400_MODE = PGSM_BAND_BIT + GSM400_EXTRA_BAND_BITS,	/* Autoband enabled */
	EGSM400_MODE = EGSM_BAND_BITS + GSM400_EXTRA_BAND_BITS,	/* Autoband enabled */
	PGSM400_DCS_MODE = PGSM_BAND_BIT + GSM400_EXTRA_BAND_BITS + DCS_BAND_BIT,	/* Autoband enabled */
	EGSM400_DCS_MODE = EGSM_BAND_BITS + GSM400_EXTRA_BAND_BITS + DCS_BAND_BIT,	/* Autoband enabled */
	PGSM400_PCS_MODE = PGSM_BAND_BIT + GSM400_EXTRA_BAND_BITS + PCS_BAND_BIT,	/* Autoband enabled */
	EGSM400_PCS_MODE = EGSM_BAND_BITS + GSM400_EXTRA_BAND_BITS + PCS_BAND_BIT,	/* Autoband enabled */
	PGSM400_PCS_MODE_LOCK = PGSM_BAND_BIT + GSM400_EXTRA_BAND_BITS + PCS_BAND_BIT + BAND_LOCK_BIT,	/* LOCK the MS to GSM400/PCS1900 mode. Autoband DISABLED. For use in testing and 400/1900 countries only */
	EGSM400_PCS_MODE_LOCK = EGSM_BAND_BITS + GSM400_EXTRA_BAND_BITS + PCS_BAND_BIT + BAND_LOCK_BIT,	/* LOCK the MS to EGSM850/PCS1900 mode. Autoband DISABLED. For use in testing and 400/1900 countries only */
	UMTS_MODE = UMTS_BAND_BIT,
	QUAD_MODE =
	    DCS_BAND_BIT + PCS_BAND_BIT + EGSM_BAND_BITS + GSM850_BAND_BIT,
	QUAD_MODE_LOCK = QUAD_MODE + BAND_LOCK_BIT,
	ALL_BITS_BAND_MODE =
	    BAND_LOCK_BIT + UMTS_BAND_BIT + GSM750_BAND_BIT + GSM850_BAND_BIT +
	    GSM400_EXTRA_BAND_BITS + EGSM_BAND_BITS + DCS_BAND_BIT +
	    PCS_BAND_BIT,
	RESET_BAND_MODE = 0,
	INVALID_BAND_MODE = 0xffff
} BandMode;
typedef enum BandTag {
	GSM_BAND = 0,
	DCS_BAND = 1,
	PCS_BAND = 2,
	EGSM_BAND = 3,
	GSM_450_BAND = 4,
	GSM_480_BAND = 5,
	GSM_850_BAND = 6,
	INVALID_BAND = 0xFF
} Band;

typedef enum TchLoopbackModeTag {
	TCH_LOOPBACK_OFF,
	TCH_LOOPBACK_MODE_A,
	TCH_LOOPBACK_MODE_B,
	TCH_LOOPBACK_MODE_C,
	TCH_LOOPBACK_MODE_D,
	TCH_LOOPBACK_MODE_E,
	TCH_LOOPBACK_MODE_F,
	TCH_LOOPBACK_MODE_I,
	INVALID_LOOPBACK_MODE = 0xffff
} TchLoopbackMode;
typedef short Arfcn;
typedef short BandModeType;
typedef INT8 TchLoopbackModeType;
typedef struct {
	BandModeType bandMode;
	Arfcn BCH_arfcn;
	Arfcn TCH_arfcn;
	INT8 pcl;
	TchLoopbackModeType loopBackType;
	short SEBitsCount;
} L1nstGSMSyncAtCommand_ts;
typedef enum {
	GSM_EstCall = 0,
	WB_EstCall,
	GSM_Meas,
	WB_Meas,
	RAT_OFF,
	NST_D2,
	MAX_NST_Command
} rpcNstOut_E;

typedef struct {
	unsigned long Result;	/*0 - Fail, 1 - Pass */
	unsigned long RespType;	/*0 - GSM_EstCall, 1 - WB_EstCall, 2 - GSM_Measurement, 3 - WB_Measurement, 4 - RAT_OFF, 5 -NST_D2 */
	unsigned long Params[6];	/*In case of measure, measured results */
} rpcNstOut_S;
/************************************ End NST Definitions **********************************************************************/

/************************************ Start LMT Definitions **********************************************************************/

#define LMT_MAX_NUM_OF_STEPS 64	/*4 bands x 16 arfcn */
typedef enum {
	LMT_GSM,
	LMT_WB,
	MAX_LMT_Command
} rpcLmtOut_E;

typedef struct {
	unsigned char band;	/* // [900 -> 3],[1800 -> 1],[1900 -> 2],[850 -> 6] */
	unsigned short arfcn;
} L1AtCmdStepParams_ts;

typedef struct {
	unsigned char band;	/* [900 -> 3],[1800 -> 1],[1900 -> 2],[850 -> 6] */
	unsigned short arfcn;
	unsigned short berTotalBits;
	unsigned short berTotalFailBits;
	unsigned short berTotalPassBits;
	signed short Rssi;
	signed short RxLevel;
} L1AtCmdStepResult_ts;

/* List Mode Test request*/
typedef struct {
	unsigned short stepCount;
	unsigned short numOfBerBits;
	L1AtCmdStepParams_ts stepParams[LMT_MAX_NUM_OF_STEPS];
} L1AtCmdGsmLmtSequenceCommand_ts;

/* List Mode Test report*/
typedef struct {
	unsigned char ratType;	/*0 - LMT_GSM, 1 - LMT_WB */
	unsigned long Result;	/*0 - SyncOK, 1 - SyncFailedOnSB, 2 - SyncFailedOnFB, 3 - SyncOKButNoTrainingSequenceDetectedForBER */
	unsigned short stepCount;
	L1AtCmdStepResult_ts stepResult[LMT_MAX_NUM_OF_STEPS];
} L1AtCmdGsmLmtSequenceReport_ts;

#define MAX_NUM_OF_FREQ 3
#define MAX_NUM_OF_ITERATION 5
typedef unsigned char LMT_RESULT_te;

enum LMT_RESULT_values {
	FAILED = 0,
	PASSED,
	FAILED_IN_DETECTED_CELL,
	FAILED_IN_PCCPCH,
	FAILED_IN_SYNC_PROC,
	FAILED_DUE_TO_LOST_SYNC
};

typedef struct {
	unsigned short UARFCN[MAX_NUM_OF_FREQ];
	unsigned char numOfFreq;
	unsigned char numOfIteration;
	unsigned short dpchScrCode;
	unsigned char dpchChanCode;
	INT8 MaxTxPower[MAX_NUM_OF_ITERATION];
	unsigned short numOfBits[MAX_NUM_OF_ITERATION];
	unsigned char pad[2];
} LMT_WB_Params_ts;

typedef struct {
	unsigned char ratType;	/*0 - LMT_GSM, 1 - LMT_WB */
	unsigned char testPassedResult;
	unsigned short goodBer[MAX_NUM_OF_FREQ][MAX_NUM_OF_ITERATION];
	unsigned short badBer[MAX_NUM_OF_FREQ][MAX_NUM_OF_ITERATION];
	unsigned short totalBer[MAX_NUM_OF_FREQ][MAX_NUM_OF_ITERATION];
	short rscp[MAX_NUM_OF_FREQ][MAX_NUM_OF_ITERATION];
	unsigned short UARFCN[MAX_NUM_OF_FREQ];
	unsigned char numOfFreq;
	unsigned char numOfIteration;
	unsigned char pad[2];
} LMT_WB_Results_ts;

typedef struct {
	unsigned char RAT;	/*WB = 1 ; GSM =2 */
	unsigned char band;
	unsigned short UARFCN;
} RSSI_Read_IN_ts;

typedef struct {
	unsigned char Result;	/*0 - Fail, 1 - Pass */
	unsigned char RAT;	/*WB = 1 ; GSM =2 */
	signed short rssi;	/* */
	unsigned char pad;
} RSSI_Read_OUT_ts;
/************************************ End LMT Definitions **********************************************************************/

unsigned long plAmMaxPowerAtCommand(unsigned short InBufLen, void *pInBuf,
				    unsigned short OutBufLen, void *pOutBuf);
unsigned long plEstWBCallAtCommand(unsigned short InBufLen, void *pInBuf,
				   unsigned short OutBufLen, void *pOutBuf);
unsigned long plMeasWBAtCommand(unsigned short InBufLen, void *pInBuf,
				unsigned short OutBufLen, void *pOutBuf);
unsigned long plEstGsmCallAtCommand(unsigned short InBufLen, void *pInBuf,
				    unsigned short OutBufLen, void *pOutBuf);
unsigned long plMeasGsmAtCommand(unsigned short InBufLen, void *pInBuf,
				 unsigned short OutBufLen, void *pOutBuf);
unsigned long L1GsmLmtSequenceAtCommand(unsigned short InBufLen, void *pInBuf,
					unsigned short OutBufLen,
					void *pOutBuf);
unsigned long plLmtWBAtCommand(unsigned short InBufLen, void *pInBuf,
			       unsigned short OutBufLen, void *pOutBuf);
unsigned long plAmUserTestAtCommand(unsigned short InBufLen, void *pInBuf,
				    unsigned short OutBufLen, void *pOutBuf);

#endif /* _L1_RPC_H_ */
