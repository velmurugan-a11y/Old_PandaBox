/*
************************************************************************************************************************
*
*  文件: app_lcr1.c
*  作者: 徐文杰
*  版本: V1.0.0
*  日期: 2023-09-03
*  描述: LCR仪表
*
************************************************************************************************************************
*/

#include "app_lcr.h"
#include <string.h>
#include <stdio.h>

#define MONI  0

static MBOOL g_bFlag4G = FALSE;

static TMR g_tmrLcrCmdID;
static TMR g_tmrLcrGetDataID;
static TMR g_tmrRebootCmdID;
static TMR g_tmrFindAddrID;
static TMR g_tmrStopCmdID;
static TMR g_tmrLinkCheckID;

static EVT g_evtLcrCmdID;
static EVT g_evtLcrSaveID;
static EVT g_evtSendHisAppID;

static E_LCR_WORK_MODE g_eLcrWorkMode = E_BRIDGE_MODE;


static MUINT8 g_u8LcrCmdInx = 0;
static MUINT8 g_u8Port = 1;

static MUINT8 g_u8ReSendCnt = 0;//重发次数

static MUINT8 g_u8FindStartAddr = 0;
static MUINT8 g_u8FindEndAddr = 0;
static MUINT8 g_u8FindPort = 0;
static MUINT8 g_u8PortStatus = 0;

static MUINT8  g_u8HisPort = 0xff;
static MUINT32 g_u32HisTime = 0;

static MBOOL  g_bGetHisFlag = FALSE;

static MBOOL  g_bSendFlag1 = FALSE;
static MBOOL  g_bSendFlag2 = FALSE;

static MUINT8 g_u8SaveNum = 0;
static MUINT8 g_u8ReSendCmdCnt = 0;
static E_CMD_ID g_eCmdID = E_CMD_ID_MAX;
static E_DEV_STATUS g_eDevStatus;
static MUINT32 g_u32Gross = 0;

static T_LCR_REV_DATA  g_stLcrData[2]; //接收到的数据
static T_LCR_SAVE_DATA g_stLcrSave[2]; //要保存的数据缓存
static T_LCR_DATA_INFO g_stLcrInfo[2]; //保存数据表头信息

static MUINT8 g_au8LastMtrCmd[2] = {3, 3};
static MUINT8 g_au8LastCmd[2] = {6, 6};

static void applcrRcvDataCmdCallback(MUINT8 *pu8Data, MUINT16 u16DataLen);
static void applcrPresetGrossCmd(MUINT8 *pu8Data, MUINT16 u16DataLen);

static void applcrGetCrc(MUINT16 *pu16Crc, MUINT8 u8Data)
{
	MINT8 s8Index;                                    
	MUINT8 u8XorData;                              

	if(pu16Crc != NULL)                      
	{
		for(s8Index = 7; s8Index >= 0; --s8Index)                    
		{
			u8XorData = (MUINT8)((*pu16Crc & 0x8000) != 0x0000);
			*pu16Crc <<= 1;                            
			*pu16Crc |= (MUINT16)((u8Data >> s8Index) & 0x01);
			if(u8XorData)  
			{
				*pu16Crc ^= 0x1021;
			}
		}
	}
}

static void applcrFillCrc(MUINT8* au8Buf, MUINT8 u8Len)
{
	MUINT8 u8Index;
	MUINT8 *pu8Data;
	MUINT16 u16Crc = 0x7E7E;

	pu8Data = au8Buf;
	
	for(u8Index = 2; u8Index < (u8Len-2); u8Index++)
	{
		applcrGetCrc(&u16Crc, pu8Data[u8Index]);
	}
	pu8Data[u8Len-2] = u16Crc%0x100;
	pu8Data[u8Len-1] = u16Crc/0x100;
	return;
}

static void applcrSendDataToAPP(MUINT8 *pu8Data, MUINT16 u16DataLen)
{
	E_SEND_MODE eSendMode = E_SEND_BT;

	eSendMode = APPCFG_GetSendMode();

	switch(eSendMode)
	{
		case E_SEND_BT:
		{
			APPBT_SendDataToBt(pu8Data, u16DataLen);
		}break;

		case E_SEND_WIFI:
		{
			APPEC20_SendDataToWifi(pu8Data,  u16DataLen);
		}break;

		case E_SEND_4G:
		{
			APPEC20_SendDataToServer(pu8Data,  u16DataLen);
		}break;
		
		default:
			break;
	}
	return;
}

static MUINT32 applcrGetData(MUINT8 *pu8Data, MUINT16 u16DataLen)
{
	MUINT8	u8Index = 0;
	MUINT8  u8LcrAddr = 0;
	MUINT8  u8Port = 0;
	MUINT8  u8ReadMode = 0;
	MUINT8	au8DataBuf[128];
	MCHAR	au8SendBuf[128];
	MUINT8	au8CliCmd[2] = {0,0};
	MUINT8	u8PortInx = 0;
	MUINT8  u8DataLen = 0;
	MUINT8  u8WritePos = 0;
	MUINT32 u32ReadAddr = 0;
	MUINT32 u32DataBuf[16];
	MUINT32 u32Result = ERR_NONE;
	MCHAR	acuEwsn[4] = {'E', 'W', 'S','N'};

	memset(au8DataBuf, 0, 128);
	MEMCPY(au8DataBuf, pu8Data, u16DataLen);

	g_bGetHisFlag = FALSE;//停止上传历史数据
	
	do
	{
	
		MEMCPY(au8DataBuf, pu8Data, u16DataLen);
		//"GetData 1,0,"
		while(au8DataBuf[u8Index] != 'a' && au8DataBuf[u8Index+1] != ' ')
		{
			u8Index++;
			if(u8Index >= u16DataLen)
			{
				break;
			}
		}
		u8Index += 2;
		for(; u8Index < u16DataLen; u8Index++)
		{
			if(au8DataBuf[u8Index] == ',')
			{
				if(u8PortInx == 1)
				{
					break;
				}
				u8PortInx++;
			}
			else
			{
				if(au8DataBuf[u8Index] >= '0' && au8DataBuf[u8Index] <= '9')
				{
					au8CliCmd[u8PortInx] = 10*au8CliCmd[u8PortInx] + (au8DataBuf[u8Index] - '0');
				}
			}
		}

		u8LcrAddr = au8CliCmd[0];
		
		u8ReadMode = au8CliCmd[1];

		if(g_stLcrInfo[0].u8DevNum == u8LcrAddr)
		{
			u8Port = 0;
		}
		else if(g_stLcrInfo[1].u8DevNum == u8LcrAddr)
		{
			u8Port = 1;
		}
		else
		{
			memset(au8SendBuf, 0, 128);
			UTIL_DataToHexString(au8SendBuf, 128, pu8Data, u16DataLen);
			DBG(DBG_E, "u8LcrAddr = %d, dev num is error %s !", u8LcrAddr, au8SendBuf);
			memset(au8SendBuf, 0, 128);
			MEMCPY(au8SendBuf, "LxGetData Error,", 16);
			break;
		}
		
		if(u8ReadMode == 0)
		{
			memset(au8SendBuf, 0, 128);
			
			if(g_eLcrWorkMode == E_BRIDGE_MODE)
			{
				MEMCPY(au8SendBuf, "LxGetData Mode 1,", 17);
				break;
			}
			MEMCPY(au8SendBuf, "LxGetData ", 10);
			u8DataLen = STRLEN(au8SendBuf);
			sprintf(&au8SendBuf[u8DataLen], "%d", g_stLcrData[u8Port].u8DevNum);
			u8DataLen = STRLEN(au8SendBuf);
			au8SendBuf[u8DataLen++]=',';
			au8SendBuf[u8DataLen++]='1';
			au8SendBuf[u8DataLen++]=',';
			
			sprintf(&au8SendBuf[u8DataLen], "%d", g_stLcrData[u8Port].u8PackNum);
			u8DataLen = STRLEN(au8SendBuf);
			au8SendBuf[u8DataLen++]=0x2C;	 

			if(g_stLcrSave[u8Port].u8WritePos >= 64)
			{
				u8WritePos = g_stLcrSave[u8Port].u8WritePos - 64;
			}
			else
			{
				u8WritePos = 256 - 64;
			}
			
			MEMCPY(u32DataBuf, &g_stLcrSave[u8Port].au8WriteBuf[u8WritePos], 64);
			
		}
		
		else if(u8ReadMode == 1)//历史数据上传
		{
			memset(au8SendBuf, 0, 128);
			MEMCPY(au8SendBuf, "LxGetDataTs ", 12);
			u8DataLen = STRLEN(au8SendBuf);
			
			if(g_stLcrInfo[u8Port].u32DataCnt == 0)
			{
				MEMCPY(au8SendBuf, "LxGetDataTs 1,", 14);
				break;
			}

			DBG(DBG_D, "g_stLcrInfo[%d].u32RdAddr = %x, g_stLcrInfo[%d].u32EndAddr = %d", u8Port, g_stLcrInfo[u8Port].u32RdAddr, u8Port, g_stLcrInfo[u8Port].u32EndAddr);

			if(g_stLcrInfo[u8Port].u32RdAddr == g_stLcrInfo[u8Port].u32EndAddr)
			{
				g_stLcrInfo[u8Port].u32RdAddr = g_stLcrInfo[u8Port].u32StartAddr;
			}
			
			g_stLcrInfo[u8Port].u32RdAddr = g_stLcrInfo[u8Port].u32RdAddr%(LCR_FLASH_TOTAL/2 - SECTOR_BYTE);

			u32ReadAddr = u8Port * LCR_START_ADDR2 + SECTOR_BYTE + g_stLcrInfo[u8Port].u32RdAddr%(LCR_FLASH_TOTAL);
			DBG(DBG_D, "u32ReadAddr = %x", u32ReadAddr);
			memset(au8DataBuf, 0, 128);
			gd25q256df_read_data(au8DataBuf, u32ReadAddr, 64);
			MEMCPY(u32DataBuf, au8DataBuf, 64);

			if(u32DataBuf[0] == 0xffffffff || u32DataBuf[0] < 160000000)
			{
				MEMCPY(au8SendBuf, "LxGetData All Read,", 19);
				DBG(DBG_D, "data is all read !");
				break;
			}
			g_stLcrInfo[u8Port].u32RdAddr += 64;
			
			sprintf(&au8SendBuf[u8DataLen], "%d,", u8LcrAddr);
			u8DataLen = STRLEN(au8SendBuf);

			au8SendBuf[u8DataLen++] = '0'; 
			au8SendBuf[u8DataLen++] = ',';

			sprintf(&au8SendBuf[u8DataLen], "%d,", g_stLcrInfo[u8Port].u32RdAddr/64);
			u8DataLen = STRLEN(au8SendBuf);
		}
		
		sprintf(&au8SendBuf[u8DataLen], "%d,", u32DataBuf[0]);//time
		u8DataLen = STRLEN(au8SendBuf);

		for(u8Index = 1; u8Index < 7; u8Index++)
		{
			if(u32DataBuf[u8Index] < 0x80000000 || u8Index >= 2)
			{
				sprintf(&au8SendBuf[u8DataLen], "%d.%01d,", u32DataBuf[u8Index]/10, u32DataBuf[u8Index]%10 );
			}
			else 
			{	 
				u32DataBuf[u8Index] = ~u32DataBuf[u8Index] + 1;
				sprintf(&au8SendBuf[u8DataLen], "-%d.%01d,", u32DataBuf[u8Index]/10, u32DataBuf[u8Index]%10 );
			}
			u8DataLen = STRLEN(au8SendBuf);
		}

		sprintf(&au8SendBuf[u8DataLen], "%d.%d,%c,", u32DataBuf[7]/2, u32DataBuf[8],acuEwsn[2+u32DataBuf[7]%2]);
		u8DataLen = STRLEN(au8SendBuf);
	
		sprintf(&au8SendBuf[u8DataLen], "%d.%d,%c", u32DataBuf[9]/2, u32DataBuf[10],acuEwsn[u32DataBuf[9]%2]);
		
	}while(0);

	u8DataLen = STRLEN(au8SendBuf);
	DBG(DBG_I, "%s", au8SendBuf);
	applcrSendDataToAPP((MUINT8 *)au8SendBuf, u8DataLen);

	return u32Result;
	
}


static MUINT32 applcrSetLcrWorkMode(E_LCR_WORK_MODE eLcrWorkMode)
{
	//SetMode 2,
	MBOOL bIsStarted = FALSE;
	MUINT8 au8SendBuf[16];
	MUINT8 u8DataLen = 0;
	MUINT8 u8PortInx = 0;
	T_BOX_PARAM stBoxParm;
	MUINT32 u32Result = ERR_NONE;
	MUINT8 au8InitCmd[9] = {0x7E, 0x7E, 0x01, 0x14, 0x02, 0x01, 0x00, 0xC4, 0xEB};

	memset(au8SendBuf, 0, 16);
	
	do
	{
		if(eLcrWorkMode > E_MODE_MAX)
		{
			u32Result = ERR_PARAM_INVALID;
			au8SendBuf[10] = '1';
			break;
		}
		
		stBoxParm = APPCFG_GetBoxParam();
		
		au8SendBuf[10] = '0';
		
		if(g_eLcrWorkMode == eLcrWorkMode)
		{
			break;
		}
		
		g_eLcrWorkMode = eLcrWorkMode;

		if(g_eLcrWorkMode == E_CMD_MODE)
		{
			g_au8LastCmd[0] = 3;
			g_au8LastCmd[1] = 3;
			g_u8LcrCmdInx = 0;
			DBG_RecoverRcvData();
			DBG_SetPrintLevel(DBG_D);
			au8InitCmd[2] = g_stLcrInfo[0].u8DevNum;
			applcrFillCrc(au8InitCmd, 9);
			HAL_UartSendData(E_HAL_UART_LCR1, au8InitCmd, 9);
			HAL_DelayMs(100);
			
			au8InitCmd[2] = g_stLcrInfo[1].u8DevNum;
			applcrFillCrc(au8InitCmd, 9);
			HAL_UartSendData(E_HAL_UART_LCR2, au8InitCmd, 9);

			memset(g_stLcrData[0].au32DataOld, 0, 24);
			memset(g_stLcrData[1].au32DataOld, 0, 24);
			
			TMR_Start(g_tmrLcrCmdID);
			if(stBoxParm.eLcrWorkMode != E_CMD_MODE)
			{
				stBoxParm.eLcrWorkMode = E_CMD_MODE;
				APPCFG_SetBoxParam(&stBoxParm);
			}
		}
		else if(g_eLcrWorkMode == E_BRIDGE_MODE)
		{
			g_au8LastCmd[0] = 2;
			g_au8LastCmd[1] = 2;
			HAL_UartSetCallback(E_HAL_UART_PRINT, applcrRcvDataCmdCallback, NULL);
			DBG_SetPrintLevel(DBG_I);
			TMR_IsStarted(g_tmrLcrCmdID, &bIsStarted);
			if(bIsStarted == FALSE)
			{
				break;
			}
			TMR_Stop(g_tmrLcrCmdID);
			for(u8PortInx = 0; u8PortInx < 2; u8PortInx++)
			{
				gd25q256df_sector_erase(u8PortInx * LCR_START_ADDR2);//擦除表头扇区
				gd25q256df_write_sector((MUINT8 *)&g_stLcrInfo[u8PortInx], u8PortInx * LCR_START_ADDR2, sizeof(T_LCR_DATA_INFO));
			}
			if(stBoxParm.eLcrWorkMode != E_BRIDGE_MODE)
			{
				stBoxParm.eLcrWorkMode = E_BRIDGE_MODE;
				APPCFG_SetBoxParam(&stBoxParm);
			}
		}
		else if(g_eLcrWorkMode == E_IDLE_MODE)
		{
			DBG_RecoverRcvData();
			TMR_IsStarted(g_tmrLcrCmdID, &bIsStarted);
			if(bIsStarted == FALSE)
			{
				break;
			}
			TMR_Stop(g_tmrLcrCmdID);
		}
		
	}while(0);

	MEMCPY(au8SendBuf, "LxSetMode ", 10); 
	u8DataLen = STRLEN(au8SendBuf);

	applcrSendDataToAPP(au8SendBuf,  u8DataLen);
	DBG(DBG_I, au8SendBuf);
	return u32Result;
}

/*
***********************************************************************************************
*  功能:设置端口对应的设备地址
*
*  描述: 无
*
*  参数: 无
*
*  返回:  无
*
***********************************************************************************************
*/
static MUINT32 applcrSetPortLcrAddr(MUINT8 *pu8Data, MUINT16 u16DataLen)
{
	MUINT8  u8Index = 0;
	MUINT8 u8DataLen = 0;
	MUINT8  u8DataBuf[36];
	MUINT8  au8SendBuf[120];
	MUINT8  au8DevNum[2] = {0,0};
	MUINT8  u8PortInx = 0;
	MUINT32 u32Result = ERR_NONE;

	memset(au8SendBuf, 0, 120);
	
	do
	{
		if(u16DataLen > 36)
		{
			break;
		}
		memset(u8DataBuf, 0, 36);
		MEMCPY(u8DataBuf, pu8Data, u16DataLen);
	    //SetPortLcrNode 0,1,
		for(u8Index = 15; u8Index < u16DataLen; u8Index++)
		{
			if(u8DataBuf[u8Index] == ',')
			{
				if(u8PortInx == 1)
				{
					break;
				}
				u8PortInx++;
			}
			else
			{
				au8DevNum[u8PortInx] = 10*au8DevNum[u8PortInx] + (u8DataBuf[u8Index] - '0');
			}
		}
		
		if(au8DevNum[0] != au8DevNum[1])
		{
			g_stLcrInfo[0].u8DevNum = au8DevNum[0];
			g_stLcrInfo[1].u8DevNum = au8DevNum[1];
		}
		else
		{
			MEMCPY(au8SendBuf, "LxSetPortLcrNode 1", 18);
			break;
		}
		
		for(u8PortInx = 0; u8PortInx < 2; u8PortInx++)
		{
			g_stLcrInfo[u8PortInx].u32Flag = 0x5a5a5a5a;
			gd25q256df_sector_erase(u8PortInx * LCR_START_ADDR2);//擦除表头扇区
			gd25q256df_write_sector((MUINT8 *)&g_stLcrInfo[u8PortInx], u8PortInx * LCR_START_ADDR2, sizeof(T_LCR_DATA_INFO));
		}
		
		MEMCPY(au8SendBuf, "LxSetPortLcrNode 0", 18); 
	}while(0);

	u8DataLen = STRLEN(au8SendBuf);
	applcrSendDataToAPP(au8SendBuf,  u8DataLen);
	DBG(DBG_I, au8SendBuf);
	return u32Result;
}

/******************************************************************************/
static MUINT32 applcrGetBoxStatus(void)
{
	MUINT32 u32Result = ERR_NONE;
	MUINT8 u8DataLen = 0;
	MCHAR  au8SendBuf[128];

	T_GPS_DATA stGpsData;
	MCHAR	acuEwsn[4] = {'E', 'W', 'S','N'};

	APPEC20_SendGpsCmd();
	
	memset(au8SendBuf, 0, 128);
	MEMCPY(&au8SendBuf[0], "LxBoxStatus ", 12);
	u8DataLen += 12;
	
	au8SendBuf[u8DataLen++] = g_eLcrWorkMode + '0';
	au8SendBuf[u8DataLen++] = ',';
	
	au8SendBuf[u8DataLen++] = '0';//预留字节
	au8SendBuf[u8DataLen++] = ',';

	sprintf(&au8SendBuf[u8DataLen], "%d", g_stLcrInfo[0].u8DevNum);
	u8DataLen = STRLEN(au8SendBuf);
	au8SendBuf[u8DataLen++] = ',';

	sprintf(&au8SendBuf[u8DataLen], "%d", g_stLcrInfo[1].u8DevNum);
	u8DataLen = STRLEN(au8SendBuf);
	au8SendBuf[u8DataLen++] = ','; 

	//gps 4字节
	u32Result = APPEC20_GetGpsData(&stGpsData);
	if(u32Result != ERR_NONE)
	{
		memset(&stGpsData, 0, sizeof(stGpsData));
	}

	sprintf(&au8SendBuf[u8DataLen], "%d.%d,%c,", stGpsData.u32Longi/2, stGpsData.u32Longimm, acuEwsn[2+stGpsData.u32Longi%2]);
	u8DataLen = STRLEN(au8SendBuf);
	
	sprintf(&au8SendBuf[u8DataLen], "%d.%d,%c,", stGpsData.u32Lati/2, stGpsData.u32Latimm, acuEwsn[stGpsData.u32Lati%2]);
	u8DataLen = STRLEN(au8SendBuf);

	sprintf(&au8SendBuf[u8DataLen], "%d", g_bFlag4G);
	u8DataLen = STRLEN(au8SendBuf);
	au8SendBuf[u8DataLen++] = ','; 

	au8SendBuf[u8DataLen++] = '1'; 
	//au8SendBuf[u8DataLen++] = ','; 

	u8DataLen = strlen(au8SendBuf);
	
	applcrSendDataToAPP((MUINT8*)au8SendBuf,  u8DataLen);

	DBG(DBG_I, au8SendBuf);
	
	return u32Result;
}


static MUINT32 applcrDeleteAll(MUINT8 *pu8Data, MUINT16 u16DataLen)//DeleteAll 23,
{
	MUINT8 u8Result = 0;
	MUINT8 u8Index = 0;
	MUINT8 u8LcrAddr = 0;
	MUINT8 u8Port = 0;
	MUINT8 au8Buf[16];
	MUINT8 au8DataBuf[32];
	MUINT32 u32Result = ERR_NONE;

	//gd25q256df_chip_erase();
	MEMCPY(au8Buf, "LxDeleteAll 0", 13);
	au8Buf[13] = 0;
	do
	{
		MEMCPY(au8DataBuf, pu8Data, u16DataLen);
		for(u8Index = 10;  u8Index < u16DataLen; u8Index++)
		{
			if(au8DataBuf[u8Index] != ',')
			{
				u8LcrAddr = 10*u8LcrAddr + (au8DataBuf[u8Index] - '0');
			}
			else
			{
				break;
			}
		}
		
		if(g_stLcrInfo[0].u8DevNum == u8LcrAddr)
		{
			u8Port = 0;
		}
		else if(g_stLcrInfo[1].u8DevNum == u8LcrAddr)
		{
			 u8Port = 1;
		}
		else
		{
			au8Buf[12] = '1';
			break;
		}
		
		g_stLcrData[u8Port].bIsRpt = 0;
		g_stLcrData[u8Port].u8DevNum = 0;
		g_stLcrData[u8Port].u8PackNum = 0;
		memset(&g_stLcrData[u8Port].au32Data, 0, 24);

		g_stLcrSave[u8Port].u32WrAddr = 0;
		g_stLcrSave[u8Port].u8WritePos = 0;

		g_stLcrInfo[u8Port].u32DataCnt = 0;
		g_stLcrInfo[u8Port].u32RdAddr = 0;
		g_stLcrInfo[u8Port].u32StartAddr = 0;
		g_stLcrInfo[u8Port].u32EndAddr = 0;
		g_stLcrInfo[u8Port].u32Flag = 0x5a5a5a5a;
		
		for(u8Index = 0; u8Index < 2; u8Index++)
		{
			u8Result = gd25q256df_sector_erase(u8Port * LCR_START_ADDR2 + u8Index*SECTOR_BYTE);//擦除表头扇区,再多删除一个扇区
			if(0 == u8Result)
			{
				break;
			}
			HAL_DelayMs(10);
		}
		if(0 == u8Result)
		{
			au8Buf[12] = '1';
			break;
		}
		u8Result = gd25q256df_write_sector((MUINT8 *)&g_stLcrInfo[u8Port], u8Port * LCR_START_ADDR2, sizeof(T_LCR_DATA_INFO));
		if(0 == u8Result)
		{
			au8Buf[12] = '1';
			break;
		}

	}while(0);
	
	applcrSendDataToAPP((MUINT8*)au8Buf,  13);
	DBG(DBG_I, au8Buf);
	
	return u32Result;
}

static MUINT32 applcrGetBoxStorage(MUINT8 *pszStrPort)
{
	MUINT8 u8DataLen = 0;
	MCHAR  au8SendBuf[128];
	MUINT8  u8Port;
	MUINT32 u32ReCont = 0;
	MUINT32 u32Result = ERR_NONE;

	memset(au8SendBuf, 0, 128);
	
	MEMCPY(&au8SendBuf[0], "LxBoxStorage ", 13);
	u8DataLen += 13;
	do
	{
		if(*pszStrPort != '2' && *pszStrPort != '1' )
		{
			au8SendBuf[13] = '0';
			au8SendBuf[14] = 0;
			u8DataLen = STRLEN(au8SendBuf);
			break;
		}
		au8SendBuf[u8DataLen++] = *pszStrPort;
		au8SendBuf[u8DataLen++] = 0x2C;

		u8Port = *pszStrPort - '1';
		g_au8LastCmd[u8Port] = 1;
		sprintf(&au8SendBuf[u8DataLen], "%d", g_stLcrInfo[u8Port].u32DataCnt);
		u8DataLen = STRLEN(au8SendBuf);
		au8SendBuf[u8DataLen++] = 0x2C;   

		u32ReCont = (LCR_FLASH_TOTAL/2 - SECTOR_BYTE)/64 - g_stLcrInfo[u8Port].u32DataCnt;

		sprintf(&au8SendBuf[u8DataLen], "%d", u32ReCont);
		u8DataLen = STRLEN(au8SendBuf);
		au8SendBuf[u8DataLen++] = 0x2C;    

		u8DataLen = STRLEN(au8SendBuf);
		//au8SendBuf[u8DataLen++] = 0x2C;  

	}while(0);

	applcrSendDataToAPP((MUINT8 *)au8SendBuf,  u8DataLen);
	DBG(DBG_I, au8SendBuf);
	return u32Result;
}

static MUINT32 applcrGetBoxHisDataTime(MUINT8 *pu8Data, MUINT16 u16DataLen)
{
	MUINT8 u8DataLen = 0;
	MUINT8 u8Index = 0;
	MUINT8 u8LcrAddr = 0;
	MUINT8 au8DataBuf[64];
	MCHAR  au8SendBuf[128];
	MUINT8  u8Port = 0;
	MUINT32 u32RdAddr = 0;
	MUINT32 u32TimeTemp = 0;
	MUINT32 u32Result = ERR_NONE;

	do
	{
		//"HisDataTime 1,"
		memset(au8DataBuf, 0, 64);
		MEMCPY(au8DataBuf, pu8Data, u16DataLen);
		for(u8Index = 12;  u8Index < u16DataLen; u8Index++)
		{
			if(au8DataBuf[u8Index] != ',')
			{
				u8LcrAddr = 10*u8LcrAddr + (au8DataBuf[u8Index] - '0');
			}
			else
			{
				break;
			}
		}

		memset(au8SendBuf, 0, 128);
		MEMCPY(au8SendBuf, "LxHisDataTime ", 14);
		u8DataLen = STRLEN(au8SendBuf);
		
		if(g_stLcrInfo[0].u8DevNum == u8LcrAddr)
		{
			u8Port = 0;
		}
		else if(g_stLcrInfo[1].u8DevNum == u8LcrAddr)
		{
			 u8Port = 1;
		}
		else
		{
			au8SendBuf[14] = '0';
			au8SendBuf[15] = ',';
			au8SendBuf[16] = '0';
			break;
		}

		if(g_stLcrInfo[u8Port].u32DataCnt == 0)
		{
			au8SendBuf[14] = '0';
			au8SendBuf[15] = ',';
			au8SendBuf[16] = '0';
			break;
		}
		
		u32RdAddr = g_stLcrInfo[u8Port].u32StartAddr + u8Port*LCR_START_ADDR2 + SECTOR_BYTE;
		memset(au8DataBuf, 0, 64);
		gd25q256df_read_data(au8DataBuf, u32RdAddr, 4);
		MEMCPY(&u32TimeTemp, au8DataBuf, 4);
		sprintf(&au8SendBuf[u8DataLen], "%d", u32TimeTemp);
		u8DataLen = STRLEN(au8SendBuf);
		au8SendBuf[u8DataLen++] = ',';

		u32RdAddr = g_stLcrInfo[u8Port].u32EndAddr + u8Port*LCR_START_ADDR2 + SECTOR_BYTE;
		u32RdAddr &= 0xfffff000;
		for(u8Index = 0;  u8Index < 64; u8Index++)
		{
			gd25q256df_read_data(au8DataBuf, u32RdAddr, 4);
			MEMCPY(&u32TimeTemp, au8DataBuf, 4);
			if(u32TimeTemp == 0xffffffff)
			{
				break;
			}
			sprintf(&au8SendBuf[u8DataLen], "%d", u32TimeTemp);
			u32RdAddr += 64;
		}
	}while(0);

	u8DataLen = STRLEN(au8SendBuf);
	au8SendBuf[u8DataLen++] = ',';
	applcrSendDataToAPP((MUINT8 *)au8SendBuf, u8DataLen);
	DBG(DBG_I, au8SendBuf);

	return u32Result;
}

static void applcrBtSendDoneCallback(MUINT8 *pu8Data, MUINT16 u16DataLen)
{
	if(g_bGetHisFlag == TRUE)
	{
		APPEC20_OpenWifiSend(FALSE);
		EVT_PostEvent(g_evtSendHisAppID, NULL);
		DBG(DBG_I, "bt send his data is ok!");
	}
	return;
}

static void applcrWifiSendDoneCallback(MUINT8 *pu8Data, MUINT16 u16DataLen)
{
	if(g_bGetHisFlag == TRUE)
	{
		APPEC20_OpenWifiSend(TRUE);
		EVT_PostEvent(g_evtSendHisAppID, NULL);
		DBG(DBG_I, "Wifi send his data is ok!");
	}
	return;
}

static void applcrSendHisDataToAppEvt(void *pArg)
{
	static MUINT16 u16SendCnt = 0;
	MUINT32 u32DataBuf[17];
	MUINT32 u32RdAddr = 0;
	MUINT8 au8DataBuf[68];
	MCHAR au8SendBuf[128];
	MUINT8 u8DataLen = 0;
	MUINT8 u8Index = 0;
	MCHAR	acuEwsn[4] = {'E', 'W', 'S','N'};

	u32RdAddr = g_stLcrInfo[g_u8HisPort].u32RdAddr + g_u8HisPort*LCR_START_ADDR2 + SECTOR_BYTE;
	gd25q256df_read_data(au8DataBuf, u32RdAddr, 68);
	MEMCPY(u32DataBuf, au8DataBuf, 68);

	memset(au8SendBuf, 0, 128);
	MEMCPY(au8SendBuf, "LxGetDataTs ", 12);
	u8DataLen = STRLEN(au8SendBuf);
	
	sprintf(&au8SendBuf[u8DataLen], "%d,", g_stLcrInfo[g_u8HisPort].u8DevNum);
	u8DataLen = STRLEN(au8SendBuf);

	sprintf(&au8SendBuf[u8DataLen], "%d,", g_stLcrInfo[g_u8HisPort].u32RdAddr/64);
	u8DataLen = STRLEN(au8SendBuf);

	sprintf(&au8SendBuf[u8DataLen], "%d,", u32DataBuf[0]);
	u8DataLen = STRLEN(au8SendBuf);

	for(u8Index = 1; u8Index < 7; u8Index++)
	{
		if(u32DataBuf[u8Index] < 0x80000000 || u8Index >= 2)
		{
			sprintf(&au8SendBuf[u8DataLen], "%d.%01d,", u32DataBuf[u8Index]/10, u32DataBuf[u8Index]%10 );
		}
		else 
		{    
			u32DataBuf[u8Index] = ~u32DataBuf[u8Index] + 1;
			sprintf(&au8SendBuf[u8DataLen], "-%d.%01d,", u32DataBuf[u8Index]/10, u32DataBuf[u8Index]%10 );
		}
		u8DataLen = STRLEN(au8SendBuf);
	}

	sprintf(&au8SendBuf[u8DataLen], "%d.%d,%c,", u32DataBuf[7]/2, u32DataBuf[8],acuEwsn[2+u32DataBuf[7]%2]);
	u8DataLen = STRLEN(au8SendBuf);

	sprintf(&au8SendBuf[u8DataLen], "%d.%d,%c", u32DataBuf[9]/2, u32DataBuf[10],acuEwsn[u32DataBuf[9]%2]);
	u8DataLen = STRLEN(au8SendBuf);

	DBG(DBG_I, "%s", au8SendBuf);

	if(g_u32HisTime >= u32DataBuf[16] && u32DataBuf[16] > 1690000000)
	{
		u16SendCnt++;
		g_bGetHisFlag = TRUE;
		g_stLcrInfo[g_u8HisPort].u32RdAddr += 64;
		au8SendBuf[u8DataLen++] = ';';
	}
	else
	{
		g_bGetHisFlag = FALSE;
		g_u8HisPort = 0xff;
		u16SendCnt = 0;
	}
	applcrSendDataToAPP((MUINT8*)au8SendBuf, u8DataLen);
	return;
}
static MUINT32 applcrGetDataTs(MUINT8 *pu8Data, MUINT16 u16DataLen)
{
	MUINT8  u8PortInx = 0;
	MUINT8  u8Index = 0;
	MUINT8  u8LcrAddr = 0;
	MUINT8  au8DataBuf[64];
	MUINT32 u32TimeTemp = 0;
	MUINT32 u32TimeTemp1 = 0;
	MUINT32 au32CliCmd[3] = {0,0,0};
	MUINT32 u32RdAddr = 0;
	MUINT32 u32OffSetAddr = 0;
	MUINT32 u32StartAddr = 0;
	MUINT32 u32EndAddr = 0;
	MUINT32 u32Result = ERR_NONE;
	
	do
	{
		MEMCPY(au8DataBuf, pu8Data, u16DataLen);
		//"GetDataTs 1,1697444385,1697542646,"
		for(u8Index = 10; u8Index < u16DataLen; u8Index++)
		{
			if(au8DataBuf[u8Index] == ',')
			{
				if(u8PortInx == 2)
				{
					break;
				}
				u8PortInx++;
			}
			else
			{
				if(au8DataBuf[u8Index] >= '0' && au8DataBuf[u8Index] <= '9' && au32CliCmd[u8PortInx] < 1000000000)
				{
					au32CliCmd[u8PortInx] = 10*au32CliCmd[u8PortInx] + (au8DataBuf[u8Index] - '0');
				}
			}
		}
		u8LcrAddr = au32CliCmd[0];
		g_u32HisTime = au32CliCmd[2];
		DBG(DBG_I, "g_u32HisTime = %d", g_u32HisTime);
		if(g_stLcrInfo[0].u8DevNum == u8LcrAddr)
		{
			g_u8HisPort = 0;
		}
		else if(g_stLcrInfo[1].u8DevNum == u8LcrAddr)
		{
			 g_u8HisPort = 1;
		}
		else
		{
			break;
		}

		u32RdAddr = g_stLcrInfo[g_u8HisPort].u32StartAddr + g_u8HisPort*LCR_START_ADDR2 + SECTOR_BYTE;
		gd25q256df_read_data(au8DataBuf, u32RdAddr, 4);
		MEMCPY(&u32TimeTemp, au8DataBuf, 4);

		if(u32TimeTemp >= au32CliCmd[1])//早于保存数据时间，则从第一条读起
		{
			g_stLcrInfo[g_u8HisPort].u32RdAddr = u32RdAddr - g_u8HisPort*LCR_START_ADDR2 - SECTOR_BYTE;
			EVT_PostEvent(g_evtSendHisAppID, NULL);
			break;
		}
		else
		{
			u32StartAddr = g_stLcrInfo[g_u8HisPort].u32StartAddr;
			u32EndAddr = g_stLcrInfo[g_u8HisPort].u32EndAddr;
			
			while(1)
			{
				u32OffSetAddr = (u32StartAddr + u32EndAddr)/2;
				u32OffSetAddr &= 0xffffffc0;
				u32RdAddr = u32OffSetAddr + g_u8HisPort*LCR_START_ADDR2 + SECTOR_BYTE;

				gd25q256df_read_data(au8DataBuf, u32RdAddr, 4);
				MEMCPY(&u32TimeTemp, au8DataBuf, 4);
				
				if(u32TimeTemp >= au32CliCmd[1])
				{
					u32RdAddr -= 64;
					gd25q256df_read_data(au8DataBuf, u32RdAddr, 4);
					MEMCPY(&u32TimeTemp1, au8DataBuf, 4);
					if(u32TimeTemp1 <= au32CliCmd[1])
					{
						g_stLcrInfo[g_u8HisPort].u32RdAddr = u32RdAddr-g_u8HisPort*LCR_START_ADDR2 - SECTOR_BYTE;
						EVT_PostEvent(g_evtSendHisAppID, NULL);
						break;
					}
					else
					{
						u32EndAddr = u32OffSetAddr;
					}
				}
				else
				{
					u32RdAddr += 64;
					gd25q256df_read_data(au8DataBuf, u32RdAddr, 4);
					MEMCPY(&u32TimeTemp1, au8DataBuf, 4);
					if(u32TimeTemp1 >= au32CliCmd[1])
					{
						g_stLcrInfo[g_u8HisPort].u32RdAddr = u32RdAddr - g_u8HisPort*LCR_START_ADDR2 - SECTOR_BYTE;
						EVT_PostEvent(g_evtSendHisAppID, NULL);
						break;
					}
					else
					{
						u32StartAddr = u32OffSetAddr;
					}
				}
			}
		}
	}while(0);

	
	return u32Result;
}

static MUINT32 applcrGetBoxInfo(void)// BoxInfo
{
	MCHAR  au8SendBuf[128];
	MUINT8 u8DataLen = 0;
	MUINT32 u32Result = ERR_NONE;

	memset(au8SendBuf, 0, 128);
	sprintf(&au8SendBuf[0], VER);
	u8DataLen = STRLEN(au8SendBuf); 

	APPEC20_GetImeiCode(&au8SendBuf[u8DataLen]);
	u8DataLen = STRLEN(au8SendBuf); 
	
	applcrSendDataToAPP((MUINT8 *)au8SendBuf,  u8DataLen);
	DBG(DBG_I, au8SendBuf);
	
	return u32Result;
}

static MUINT32 applcrGetBoxTime(void)
{
	MCHAR  au8SendBuf[128];
	MUINT8 u8DataLen = 0;
	MUINT32 u32Result = ERR_NONE;
	MUINT32 u32Second = 0;

	memset(au8SendBuf, 0, 128);
	g_au8LastCmd[0] = 0;
	g_au8LastCmd[1] = 0;
	u32Second = RTC_GetDateTimeToSec();
	MEMCPY(&au8SendBuf[0], "LxBoxTime ", 10);
	
	sprintf(&au8SendBuf[10], "%d", u32Second);
	u8DataLen = STRLEN(au8SendBuf);
	
	applcrSendDataToAPP((MUINT8 *)au8SendBuf,  u8DataLen);
	
	DBG(DBG_I, au8SendBuf);
	
	return u32Result;
}

static MUINT32 applcrSetBoxTime(MUINT32 u32Second)
{
	MCHAR  au8SendBuf[128];
	MUINT8 u8DataLen = 0;
	MUINT32 u32Result = ERR_NONE;

	RTC_SetDateTime(u32Second);
	
	memset(au8SendBuf, 0, 128);
	MEMCPY(&au8SendBuf, "LxSetBoxTime 0", 14);

	u8DataLen = STRLEN(au8SendBuf);

	applcrSendDataToAPP((MUINT8 *)au8SendBuf,  u8DataLen);
	
	DBG(DBG_I, au8SendBuf);
	
	return u32Result;
}

void applcrReadBtName(void)
{
	MUINT8 au8SendBuf[64];
	MUINT8 u8DataLen = 0;
	T_BOX_PARAM stBoxParam;

	memset(&stBoxParam, 0, sizeof(T_BOX_PARAM));
	stBoxParam = APPCFG_GetBoxParam();

	memset(au8SendBuf, 0, 64);
	MEMCPY(&au8SendBuf[0], "LxRdBtName ", 11);
	u8DataLen = 11;
	
	MEMCPY(&au8SendBuf[u8DataLen], stBoxParam.au8BtName, STRLEN(stBoxParam.au8BtName));
	u8DataLen = STRLEN(au8SendBuf);

	applcrSendDataToAPP(au8SendBuf, u8DataLen);
	return;
}

static MUINT32 applcrRdPortLcrNum(void)
{
	MCHAR au8SendBuf[128];
	MUINT8 u8DataLen = 0;
	MUINT32 u32Result = ERR_NONE;

	memset(au8SendBuf, 0, 128);
	MEMCPY(au8SendBuf, "LxRdPortLcrNode ", 16);
	u8DataLen = 16;

	sprintf(&au8SendBuf[u8DataLen], "%d", g_stLcrInfo[0].u8DevNum);
	u8DataLen = STRLEN(au8SendBuf);
	au8SendBuf[u8DataLen++] = ',';

	sprintf(&au8SendBuf[u8DataLen], "%d", g_stLcrInfo[1].u8DevNum);
	u8DataLen = STRLEN(au8SendBuf);
	//au8SendBuf[u8DataLen++] = ','; 

	g_au8LastCmd[0] = 4;
	g_au8LastCmd[1] = 4;
	
	applcrSendDataToAPP((MUINT8*)au8SendBuf,  u8DataLen);
	DBG(DBG_I, au8SendBuf);
	
	return u32Result;
}

static void applcrGetLcrNodeTimeOut(TMR u8Tmr, void *pArg)
{
		                    //-head---,-to-,from-,sta,len ,data1  0 , crc0 crc1
	MUINT8  au8CmdByte[] = {0x7E,0x7E,0x01,0x14,0x01,0x01,0x28,0x00,0x00};
	//MUINT8  au8CmdByte[] = {0x7E,0x7E,0x01,0x14,0x01,0x02,0x20,0x02,0x77,0xCD};  //GrossQty
	MCHAR  au8SendBuf[32];
	MUINT8  au8TempStr[128];
	MUINT8  u8DataLen = 0;
	
	au8CmdByte[2] = g_u8FindStartAddr;
	//au8CmdByte[3] = E_FIND_ADDER;
	
	g_eCmdID = E_FIND_ADDER;
	
	if(g_u8FindStartAddr < g_u8FindEndAddr)
	{
		applcrFillCrc(au8CmdByte, 9);
		
		if(g_u8FindPort%2 == 1)
		{
			g_bSendFlag1 = !g_bSendFlag1;
			au8CmdByte[4] = g_bSendFlag1;
			HAL_UartSendData(E_HAL_UART_LCR1, au8CmdByte, 6);
			memset(au8TempStr, 0, 128);
			UTIL_DataToHexString((MCHAR*)au8TempStr, 128, au8CmdByte, 9);
			//DBG(DBG_I, au8TempStr);
		}
		else if(g_u8FindPort%2 == 0)
		{
			g_bSendFlag2 = !g_bSendFlag2;
			au8CmdByte[4] = g_bSendFlag2;
			HAL_UartSendData(E_HAL_UART_LCR2, au8CmdByte, 9);
		}
		TMR_Restart(g_tmrFindAddrID);
		g_u8FindStartAddr++;
	}
	
	else
	{
		if(g_u8FindPort < 3)
		{
			TMR_Kill(g_tmrFindAddrID);
			memset(au8SendBuf, 0, 32);
			sprintf((MCHAR*)au8SendBuf,"LxFindLcrNode 0");
			u8DataLen = STRLEN(au8SendBuf);
			applcrSendDataToAPP((MUINT8 *)au8SendBuf,  u8DataLen);
			DBG(DBG_I, "not find node address!");
		}
		else if(g_u8FindPort == 3)
		{
			g_u8PortStatus = 0x00;
			g_u8FindPort = 4;
			g_u8FindStartAddr = 1;
			g_u8FindEndAddr = 255;
			TMR_Restart(g_tmrFindAddrID);
		}

		else if(g_u8FindPort == 4)
		{
			TMR_Kill(g_tmrFindAddrID);
			memset(au8SendBuf, 0, 32);
			sprintf(au8SendBuf,"LxRdRegister %d,0,", g_u8PortStatus%2);
			u8DataLen = STRLEN(au8SendBuf);
			DBG(DBG_I, au8SendBuf);
			applcrSendDataToAPP((MUINT8 *)au8SendBuf,  u8DataLen);
			
		}
	}
	return;
}

static MUINT32 applcrGetLcrNode(MUINT8 *pu8Data, MUINT16 u16DataLen)
{
	MUINT8 u8Index = 0;
	MUINT8 u8PortInx = 0;
	MUINT8 au8DataBuf[64]; 
	MUINT8 au8CliCmd[3] = {0,0,0};
	MUINT8 au8SendBuf[32];
	MUINT8 u8DataLen = 0;
	MUINT32 u32Result = ERR_NONE;

	MEMCPY(au8DataBuf, pu8Data, u16DataLen);
	//"GetLcrNode 1,3,60,"
	for(u8Index = 11; u8Index < u16DataLen; u8Index++)
	{
		if(au8DataBuf[u8Index] == ',')
		{
			if(u8PortInx == 2)
			{
				break;
			}
			u8PortInx++;
		}
		else
		{
			au8CliCmd[u8PortInx] = 10*au8CliCmd[u8PortInx] + (au8DataBuf[u8Index] - '0');
		}
	}

	if(g_stLcrData[au8CliCmd[0]-1].u8HandCnt)
	{
		memset(au8SendBuf, 0, 32);
		sprintf((MCHAR*)au8SendBuf,"LxFindLcrNode %d,%d", au8CliCmd[0], g_stLcrData[au8CliCmd[0]-1].u8DevNum);
		u8DataLen = STRLEN(au8SendBuf);
		DBG(DBG_I, au8SendBuf);
		applcrSendDataToAPP((MUINT8 *)au8SendBuf,  u8DataLen);
	}
	else
	{
		g_u8FindPort = au8CliCmd[0];
		g_u8FindStartAddr = au8CliCmd[1];
		g_u8FindEndAddr = au8CliCmd[2];
		u32Result = TMR_Creat(5, applcrGetLcrNodeTimeOut, NULL, &g_tmrFindAddrID);
    	ASSERT(ERR_NONE == u32Result);
    	TMR_Start(g_tmrFindAddrID);
	}
	return u32Result;
}


static void applcrRdRegister(void)
{
	MUINT32 u32Result = ERR_NONE;
	
	g_u8FindPort = 3;
	g_u8FindStartAddr = 1;
	g_u8FindEndAddr = 255;
	g_u8PortStatus = 0;
	u32Result = TMR_Creat(5, applcrGetLcrNodeTimeOut, NULL, &g_tmrFindAddrID);
	ASSERT(ERR_NONE == u32Result);
	TMR_Start(g_tmrFindAddrID);
	
	return;
}

static MUINT32 applcrSendCmdToLcr(MUINT8 *pu8Data)
{
	MUINT8  au8CmdByte[10];
	MUINT8  au8Temp[64];
	MUINT32 u32Result = ERR_NONE;
	
	do
	{
		MEMCPY(au8CmdByte, pu8Data, 10);
		
		applcrFillCrc(au8CmdByte, 10);
		
		if(g_stLcrInfo[0].u8DevNum == au8CmdByte[2])
		{
			//g_bSendFlag1 = !g_bSendFlag1;
			//au8CmdByte[4] = g_bSendFlag1;
			HAL_UartSendData(E_HAL_UART_LCR1, au8CmdByte, 10);
		}
		else if(g_stLcrInfo[1].u8DevNum == au8CmdByte[2])
		{
			//g_bSendFlag2 = !g_bSendFlag2;
			//au8CmdByte[4] = g_bSendFlag2;
			HAL_UartSendData(E_HAL_UART_LCR2, au8CmdByte, 10);
		}
		else
		{
			//HAL_UartSendData(E_HAL_UART_LCR2, au8CmdByte, 10);
			DBG(DBG_I, "---ERR_PARAM_INVALID---%d", au8CmdByte[2]);
			u32Result = ERR_PARAM_INVALID;
			break;
		}
		UTIL_DataToHexString((MCHAR *)au8Temp, 64, au8CmdByte, 10);
		DBG(DBG_D, "---lcr2 send data-%s", au8Temp);
	}while(0);
	
	return u32Result;
}

static void applcrStartOrStopCmd(MUINT8 u8LcrAddr, E_CMD_ID eCmdID)  //"Start 1,"
{
	                       //-head---,-to-,from-,sta,len ,data1  0 , crc0 crc1
	MUINT8  au8CmdByte[] = {0x7E,0x7E,0x01,0x14,0x01,0x02,0x24,0x00,0x00,0x00};
	MUINT32 u32Result = ERR_NONE;

	do
	{
		if(eCmdID > E_CMD_ID_MAX)
		{
			break;
		}
		
		au8CmdByte[2] = u8LcrAddr;
		au8CmdByte[7] = eCmdID - E_START_ID;
		g_eCmdID = eCmdID;
		g_u8SaveNum = u8LcrAddr;
			
		if(g_stLcrInfo[g_u8Port].u8DevNum != g_u8SaveNum)
		{
			DBG(DBG_I, "---------------send %d----------------!", g_eCmdID);
			u32Result = applcrSendCmdToLcr(au8CmdByte);
			if(u32Result != ERR_NONE)
			{
				DBG(DBG_I, "---node num is error!");
			}
			else
			{
				TMR_Stop(g_tmrLcrGetDataID);
				if(g_u8Port == 0)
				{
					memset(g_stLcrData[1].au32DataOld, 0, 24);
				}
				//TMR_Start(g_tmrStopCmdID);
			}
		}
	}while(0);
	
	return;
}




static void applcrStopCmdTimeOut(TMR u8Tmr, void *pArg)
{
	MUINT8 au8SendBuf[32];
	MUINT8 u8DataLen = 0;

	do
	{
		if(g_eCmdID == E_CMD_ID_MAX)
		{
			break;
		}
		
		DBG(DBG_I, "--applcrStopCmdTimeOut--%d--%d--%d", g_u8SaveNum, g_eCmdID, g_u8ReSendCmdCnt);
		g_u8ReSendCmdCnt++;

		if(g_u8ReSendCmdCnt < 3)
		{
			if(g_eCmdID <= E_STOP_ID)
			{
				applcrStartOrStopCmd(g_u8SaveNum, g_eCmdID);
			}
			else if(g_eCmdID == E_PRINT_ID)
			{
				sprintf((char*)au8SendBuf, "Print %d,", g_u8SaveNum);
				u8DataLen = STRLEN(au8SendBuf);
				applcrPrintCmd(au8SendBuf, u8DataLen);
			}
		}
	}while(0);
	return;
}

static void applcrLinkCheckTimeOut(TMR u8Tmr, void *pArg)
{
	if(HAL_GpioGetInStatus(E_HAL_GPIO_I_PORT1))
	{
		HAL_GpioReset(E_HAL_GPIO_O_LCR1_LED);
	}
	else
	{
		HAL_GpioSet(E_HAL_GPIO_O_LCR1_LED);
	}
	
	if(HAL_GpioGetInStatus(E_HAL_GPIO_I_PORT2))
	{
		HAL_GpioReset(E_HAL_GPIO_O_LCR2_LED);
	}
	else
	{
		HAL_GpioSet(E_HAL_GPIO_O_LCR2_LED);
	}
	return;
}

static void applcrGetSwitchStateCmd(MUINT8 *pu8Data, MUINT16 u16DataLen)  //"SwitchState 1,"
{
	MUINT8  u8Index = 0;
	MUINT8  u8LcrAddr = 0;
	MUINT8  au8DataBuf[32];
	                       //-head---,-to-,from-,sta,len ,data1  0 , crc0 crc1
	MUINT8  au8CmdByte[] = {0x7E,0x7E,0x01,0x15,0x01,0x01,0x28,0x00,0x00};

	do
	{
		memset(au8DataBuf, 0, 32);
		ASSERT(u16DataLen <= 32);
		MEMCPY(au8DataBuf, pu8Data, u16DataLen);
		for(u8Index = 12;  u8Index < u16DataLen; u8Index++)
		{
			if(au8DataBuf[u8Index] != ',')
			{
				u8LcrAddr = 10*u8LcrAddr + (au8DataBuf[u8Index] - '0');
			}
			else
			{
				break;
			}
		}
		
		au8CmdByte[2] = u8LcrAddr;
		au8CmdByte[3] = E_STATE_ID;
		applcrFillCrc(au8CmdByte, 9);
		
	}while(0);
	
	return;
}

static void applcrModifyLcrNodeCmd(MUINT8 *pu8Data, MUINT16 u16DataLen)
{
	MUINT8  u8Index = 0;
	MUINT8  u8PortInx = 0;
	MUINT8  au8DataBuf[10];
	MUINT8  au8CliCmd[3] = {0,0,0};
						   //-head---,-to-,from-,sta,len ,data1  0 , crc0 crc1
	MUINT8  au8CmdByte[] = {0x7E,0x7E,0x01,0x14,0x01,0x02,0x25,0xFA,0x77,0xCD};

	do
	{
		MEMCPY(au8DataBuf, pu8Data, u16DataLen);
		//"ModifyLcrNode,1,12,23,"
		for(u8Index = 14; u8Index < u16DataLen; u8Index++)
		{
			if(au8DataBuf[u8Index] == ',')
			{
				if(u8PortInx == 2)
				{
					break;
				}
				u8PortInx++;
			}
			else
			{
				au8CliCmd[u8PortInx] = 10*au8CliCmd[u8PortInx] + (au8DataBuf[u8Index] - '0');
			}
		}

		au8CmdByte[2] = au8CliCmd[1];
		//au8CmdByte[3] = E_MDF_LCR_ND_ID;
		g_eCmdID = E_CMD_ID_MAX;
		au8CmdByte[7] = au8CliCmd[2];
		applcrSendCmdToLcr(au8CmdByte);
	}while(0);
	return;
}


static void applcrPresetGrossCmd(MUINT8 *pu8Data, MUINT16 u16DataLen)
{
	MUINT8  u8Index = 0;
	MUINT8  u8LcrAddr = 0;
	MUINT8  u8PortInx = 0;
	MUINT8  au8DataBuf[64];//-head---,-to-,from-,sta,len ,data5  4 ,  3,   2    1    3 ,crc0 crc1
	MUINT8  au8CmdByte[14] = {0x7E,0x7E,0x01,0x15,0x01,0x06,0x21,0x05,0x00,0x00,0x00,0x00,0x77,0xCD};
	static  MBOOL bFlag = 0;
	
	MUINT32  u32Gross = 0;

	g_u32Gross = 0;
	
	do
	{
		memset(au8DataBuf, 0, 64);
		MEMCPY(au8DataBuf, pu8Data, u16DataLen);
		//"PresetGross 1,123456.7,"
		for(u8Index = 12; u8Index < u16DataLen; u8Index++)
		{
			if(au8DataBuf[u8Index] == ',')
			{
				if(u8PortInx == 1)
				{
					break;
				}
				u8PortInx++;
			}
			else
			{
				if(u8PortInx == 0)
				{
					u8LcrAddr = 10*u8LcrAddr + (au8DataBuf[u8Index] - '0');
				}
				else if(au8DataBuf[u8Index] != '.')
				{
					g_u32Gross = 10*g_u32Gross + (au8DataBuf[u8Index] - '0');
				}
			}
		}

		au8CmdByte[2] = u8LcrAddr;
		g_eCmdID = E_RE_GR_ID;
		g_u8SaveNum = u8LcrAddr;

		DBG(DBG_I, "---------send %d---%d--!", g_eCmdID, g_u8SaveNum);
		
		if(g_stLcrInfo[g_u8Port].u8DevNum == g_u8SaveNum)
		{
			break;
		}

		u32Gross = UTIL_SwapU32Data(g_u32Gross);

		MEMCPY(&au8CmdByte[8], &u32Gross, 4);
		au8CmdByte[4] = bFlag;
		bFlag = !bFlag;
		applcrFillCrc(au8CmdByte, 14);
		
		if(g_stLcrInfo[0].u8DevNum == u8LcrAddr)
		{
			HAL_UartSendData(E_HAL_UART_LCR1, au8CmdByte, 14);
			g_au8LastCmd[0] = 5;
		}
		else if(g_stLcrInfo[1].u8DevNum == u8LcrAddr)
		{
			HAL_UartSendData(E_HAL_UART_LCR2, au8CmdByte, 14);
			g_au8LastCmd[1] = 5;
		}
		memset(au8DataBuf, 0, 64);
		UTIL_DataToHexString((MCHAR*)au8DataBuf, 64, au8CmdByte, 14);
		DBG(DBG_I, au8DataBuf);
	}while(0);
}

static void applcrGetLastMtrCmd(MUINT8 *pszStrPort)
{
	MUINT8 u8DataLen = 0;
	MCHAR  au8SendBuf[128];
	MUINT8	u8Port;
	MUINT8 * const apszLcrCmd[] = {"Start", "Pause", "Stop","None"};
	
	memset(au8SendBuf, 0, 128);
	
	MEMCPY(&au8SendBuf[0], "LxGetLastMtrCmd 1,0", 19);
	au8SendBuf[16] = *pszStrPort;
	
	u8DataLen += 17;
	do
	{
		if(*pszStrPort != '2' && *pszStrPort != '1' )
		{
			au8SendBuf[18] = '1';
			au8SendBuf[19] = 0;
			u8DataLen = STRLEN(au8SendBuf);
			break;
		}
		
		u8Port = *pszStrPort - '1';
		sprintf(&au8SendBuf[18], "%s", apszLcrCmd[g_au8LastMtrCmd[u8Port]]);
		
		u8DataLen = STRLEN(au8SendBuf);
		au8SendBuf[u8DataLen++] = 0x2C; 
		if(g_au8LastMtrCmd[u8Port] > 2)
		{
			au8SendBuf[u8DataLen++] = '1';  
		}
		au8SendBuf[u8DataLen++] = 0x2C;    
		u8DataLen = STRLEN(au8SendBuf);
	}while(0);

	applcrSendDataToAPP((MUINT8 *)au8SendBuf,  u8DataLen);
	DBG(DBG_I, au8SendBuf);
	return;
}


static void applcrGetLastCmd(MUINT8 *pszStrPort)
{
	MUINT8 u8DataLen = 0;
	MCHAR  au8SendBuf[128];
	MUINT8	u8Port;
	MUINT8 * const apszLcrCmd[] = {"BoxTime", "BoxStorage", "Mode 1","Mode 2", "RdPortLcrNode","PresetGross","None"};
	
	memset(au8SendBuf, 0, 128);
	
	MEMCPY(&au8SendBuf[0], "LxGetLastCmd 1,0", 16);
	au8SendBuf[16] = *pszStrPort;
	
	u8DataLen += 17;
	do
	{
		if(*pszStrPort != '2' && *pszStrPort != '1' )
		{
			au8SendBuf[15] = '1';
			au8SendBuf[16] = 0;
			u8DataLen = STRLEN(au8SendBuf);
			break;
		}
		
		u8Port = *pszStrPort - '1';
		sprintf(&au8SendBuf[15], "%s", apszLcrCmd[g_au8LastCmd[u8Port]]);
		
		u8DataLen = STRLEN(au8SendBuf);
		au8SendBuf[u8DataLen++] = 0x2C; 
		if(g_au8LastCmd[u8Port] > 3)
		{
			au8SendBuf[u8DataLen++] = '1';  
		}
		else
		{
			au8SendBuf[u8DataLen++] = '0';  
		}
		au8SendBuf[u8DataLen++] = 0x2C;    
		u8DataLen = STRLEN(au8SendBuf);
	}while(0);

	applcrSendDataToAPP((MUINT8 *)au8SendBuf,  u8DataLen);
	DBG(DBG_I, au8SendBuf);
	return;
}

static void applcrSaveDataEvt(void *pArg)
{
	MUINT8 u8PortInx = 0;
	MUINT32 u32Second;
	MUINT32 u32WriteAddr = 0;
	MUINT32 u32Result = ERR_NONE;
	T_GPS_DATA stGpsData;
	
	for(u8PortInx = 0; u8PortInx < 2; u8PortInx++)
	{
		if(FALSE == g_stLcrData[u8PortInx].bIsRpt)
		{
			continue;
		}
		
		//预留一个扇区的空间做表头
		u32WriteAddr = u8PortInx * LCR_START_ADDR2;
		
		g_stLcrData[u8PortInx].bIsRpt = FALSE;
		
		if(g_stLcrInfo[0].u8DevNum == g_stLcrData[u8PortInx].u8DevNum)
		{
			u8PortInx = 0;
		}
		else
		{
			DBG(DBG_I, "DevNum is change, erase data!");
			return;
		}
		u32Second = RTC_GetDateTimeToSec();

		//gps 4字节
		u32Result = APPEC20_GetGpsData(&stGpsData);
		if(u32Result != ERR_NONE)
		{
			memset(&stGpsData, 0, sizeof(stGpsData));
		}
		//time 4字节
		MEMCPY(&g_stLcrSave[u8PortInx].au8WriteBuf[g_stLcrSave[u8PortInx].u8WritePos], &u32Second, sizeof(MUINT32));
		g_stLcrSave[u8PortInx].u8WritePos += 4;

		MEMCPY(&g_stLcrSave[u8PortInx].au8WriteBuf[g_stLcrSave[u8PortInx].u8WritePos], &g_stLcrData[u8PortInx].au32Data[0], 24);
		g_stLcrSave[u8PortInx].u8WritePos += 24;

		MEMCPY(&g_stLcrSave[u8PortInx].au8WriteBuf[g_stLcrSave[u8PortInx].u8WritePos], &stGpsData.u32Longi, sizeof(MUINT32));
		g_stLcrSave[u8PortInx].u8WritePos += 4;

		MEMCPY(&g_stLcrSave[u8PortInx].au8WriteBuf[g_stLcrSave[u8PortInx].u8WritePos], &stGpsData.u32Longimm, sizeof(MUINT32));
		g_stLcrSave[u8PortInx].u8WritePos += 4;

		MEMCPY(&g_stLcrSave[u8PortInx].au8WriteBuf[g_stLcrSave[u8PortInx].u8WritePos], &stGpsData.u32Lati, sizeof(MUINT32));
		g_stLcrSave[u8PortInx].u8WritePos += 4;

		MEMCPY(&g_stLcrSave[u8PortInx].au8WriteBuf[g_stLcrSave[u8PortInx].u8WritePos], &stGpsData.u32Latimm, sizeof(MUINT32));
		g_stLcrSave[u8PortInx].u8WritePos += 4;

		g_stLcrSave[u8PortInx].u8WritePos += 20;//预留20个字节，对齐

		if((g_stLcrSave[u8PortInx].u32WrAddr + SECTOR_BYTE) >= LCR_FLASH_TOTAL/2)//0x1000000
		{
			g_stLcrInfo[u8PortInx].u32EndAddr = 0;
			g_stLcrSave[u8PortInx].u32WrAddr = 0;
			g_stLcrInfo[u8PortInx].u32StartAddr = 0;//一个扇区保存头信息
		}
		
		u32WriteAddr = u32WriteAddr + g_stLcrSave[u8PortInx].u32WrAddr + SECTOR_BYTE;
		g_stLcrInfo[u8PortInx].u32DataCnt++;

		if(g_stLcrSave[u8PortInx].u8WritePos == 0)
		{
			if(u32WriteAddr%SECTOR_BYTE == 0)//一个扇区长度保存表头信息
			{
				gd25q256df_sector_erase(u8PortInx * LCR_START_ADDR2);//擦除表头扇区
				HAL_DelayMs(10);
				g_stLcrInfo[u8PortInx].u32Flag = 0x5a5a5a5a;
				gd25q256df_write_sector((MUINT8 *)&g_stLcrInfo[u8PortInx], u8PortInx * LCR_START_ADDR2, sizeof(T_LCR_DATA_INFO));
				HAL_DelayMs(10);
				gd25q256df_sector_erase(u32WriteAddr);//擦除保存数据扇区
				HAL_DelayMs(10);
				if(g_stLcrInfo[u8PortInx].u32StartAddr == g_stLcrInfo[u8PortInx].u32EndAddr && g_stLcrInfo[u8PortInx].u32DataCnt > 300)
				{
					g_stLcrInfo[u8PortInx].u32StartAddr += SECTOR_BYTE;
					g_stLcrInfo[u8PortInx].u32DataCnt -= 64;//64条数据占一个扇区
					if(g_stLcrInfo[u8PortInx].u32RdAddr <= g_stLcrInfo[u8PortInx].u32StartAddr)
					{
						g_stLcrInfo[u8PortInx].u32RdAddr = g_stLcrInfo[u8PortInx].u32StartAddr;
					}
				}
				DBG(DBG_I, "sector_erase, ADDR: %x, u32DataCnt = %d", u32WriteAddr, g_stLcrInfo[u8PortInx].u32DataCnt);
			}
			gd25q256df_write_sector((MUINT8 *)&g_stLcrSave[u8PortInx].au8WriteBuf, u32WriteAddr, PAGE_SIZE);
			g_stLcrSave[u8PortInx].u32WrAddr += PAGE_SIZE;//偏移地址
			g_stLcrInfo[u8PortInx].u32EndAddr = g_stLcrSave[u8PortInx].u32WrAddr;
			DBG(DBG_I, "u32StartAddr = %x, u32EndAddr: %x, u32DataCnt = %d", g_stLcrInfo[u8PortInx].u32StartAddr, g_stLcrInfo[u8PortInx].u32EndAddr, g_stLcrInfo[u8PortInx].u32DataCnt);
		}
	}
	return;
}




/*
***********************************************************************************************
*  功能:接收到模块数据回调函数
*
*  描述: 无
*
*  参数: 无
*
*  返回:  无
*
***********************************************************************************************
*/
static void applcrDataEvtProcess(MUINT8 *pu8Data, MUINT16 u16DataLen)
{
	MCHAR  au8Temp[128];
	MCHAR  au8SendBuf[128];
	MUINT8 u8DataLen = 0;
	MUINT8 u8StateInx = 0;
	MUINT8 u8Port = 0;
	MCHAR  au8TempStr[128];
	MUINT32 u32Temp = 0;
	MUINT32 u32Temp4 = 0;
	MUINT32 au32DataTemp[6];
	MUINT8 au8InitCmd[9] = {0x7E, 0x7E, 0x01, 0x14, 0x02, 0x01, 0x00, 0xC4, 0xEB};

	MUINT8 * const apszLcrCmd[] = {"LxStart 0", "LxPause 0", "LxStop 0","LxPrint 0","LxModifytLcrNode 0", "LxPresetGross 0", "LxSwitchState "};
	MUINT8 * const apszLcrSta[] = {"Run", "Stop", "Print", "Shift Print"};

	if(*pu8Data != 0x7e || u16DataLen > 128)
	{
		DBG(DBG_D, "---Recice Test data-%s", pu8Data);
		return;
	}
	else
	{
		memset(au8TempStr, 0, 128);
		UTIL_DataToHexString(au8TempStr, 128, pu8Data, u16DataLen);
	}

	do
	{
		TMR_Stop(g_tmrLcrGetDataID);
		
		if(g_eLcrWorkMode == E_BRIDGE_MODE)
		{
			//APPBT_SendDataToBt(pu8Data, u16DataLen);
			//01 05 0A 30 31 32 33 34 35 36 37 38 39
			memset(au8SendBuf, 0, 128);
			au8SendBuf[u8DataLen++] = 0x01;
			au8SendBuf[u8DataLen++] = 0x05;
			au8SendBuf[u8DataLen++] = u16DataLen;
			MEMCPY(&au8SendBuf[u8DataLen], pu8Data, u16DataLen);
			u8DataLen += u16DataLen;

			HAL_UartSendData(E_HAL_UART_BT, (MUINT8*)au8SendBuf, u8DataLen);
			DBG(DBG_D, "---lcr2 rev data-%s", au8TempStr);
			//HAL_UartSendData(E_HAL_UART_PRINT, pu8Data, u16DataLen);
			break;
		}
		
		memset(au8Temp, 0, 128);
		MEMCPY(au8Temp, pu8Data, u16DataLen);

		if((u16DataLen == 14 || au8Temp[5] == 6) && au8Temp[2] == 0x14)//判断是否为获取数据包
		{
			MEMCPY(&u32Temp, &au8Temp[8], 4);
#if MONI
			if(g_u8Port == 0)
			{
				au8Temp[3] -= 14;
			}
#endif
			
			if(g_stLcrInfo[0].u8DevNum == au8Temp[3])
			{
				g_u8Port = 0;
			}
			else if(g_stLcrInfo[1].u8DevNum == au8Temp[3])
			{
				g_u8Port = 1;
			}
			else
			{
				DBG(DBG_E, "receive data devnum error!");
				break;
			}
			
			g_stLcrData[g_u8Port].u8DevNum = au8Temp[3];
			g_stLcrData[g_u8Port].au32Data[g_u8LcrCmdInx] = UTIL_SwapU32Data(u32Temp);
			
			if(g_stLcrData[g_u8Port].au32Data[g_u8LcrCmdInx] != g_stLcrData[g_u8Port].au32DataOld[g_u8LcrCmdInx])
			{
				DBG(DBG_D, "--lcr2 rev data--%d--%d--%s--update--%d--%d--", u16DataLen, g_u8LcrCmdInx, au8TempStr, 
				g_stLcrData[g_u8Port].au32DataOld[g_u8LcrCmdInx], g_stLcrData[g_u8Port].au32Data[g_u8LcrCmdInx]);
			}
			else
			{
				DBG(DBG_D, "--lcr2 rev data--%d--%d--%s--same--%d--", u16DataLen, g_u8LcrCmdInx, au8TempStr, g_stLcrData[g_u8Port].au32Data[g_u8LcrCmdInx]);
			}
			
			g_u8LcrCmdInx++;
			
			if(g_u8LcrCmdInx == 3)
			{
				APPEC20_SendGpsCmd();
			}
			
			if(g_u8LcrCmdInx == 6)//端口2取数据完成
			{
				if(0 != memcmp(g_stLcrData[g_u8Port].au32Data, g_stLcrData[g_u8Port].au32DataOld, 24))
				{
					//仪表值为0，有START复位
					if((g_stLcrData[g_u8Port].au32Data[0] == 0)||(g_stLcrData[g_u8Port].au32Data[0] >= g_stLcrData[g_u8Port].au32DataOld[0]))
					{
						MEMCPY(au32DataTemp, g_stLcrData[g_u8Port].au32Data, 24);
						//646301						130                          64171
						u32Temp = g_stLcrData[g_u8Port].au32Data[0] + g_stLcrData[g_u8Port].au32Data[4];
						if(u32Temp > g_stLcrData[g_u8Port].au32Data[2])
						{
							g_stLcrData[g_u8Port].au32Data[2] = u32Temp;
							DBG(DBG_D, "u32Temp is big, %d------%d",u32Temp, g_stLcrData[g_u8Port].au32Data[2]);
						}//                          64640
						else if(u32Temp < g_stLcrData[g_u8Port].au32Data[2])
						{   // 646270                64640                             130
							u32Temp4 = g_stLcrData[g_u8Port].au32Data[2] - g_stLcrData[g_u8Port].au32Data[0];
							if((0 == g_stLcrData[g_u8Port].au32Data[1]) && (u32Temp4 - g_stLcrData[g_u8Port].au32Data[4] > 80))//仪表值没有增加的情况下，是初始累加值不对
							{
								g_stLcrData[g_u8Port].au32Data[4] = u32Temp4;
							}
							else
							{
								g_stLcrData[g_u8Port].au32Data[2] = u32Temp;
							}
							DBG(DBG_D, "u32Temp is small, %d------%d",u32Temp, g_stLcrData[g_u8Port].au32Data[2]);
						}
						else
						{
							DBG(DBG_D, "data2 is ok!, %d------%d",u32Temp, g_stLcrData[g_u8Port].au32Data[2]);
						}
						g_stLcrData[g_u8Port].u8PackNum++;
						g_stLcrData[g_u8Port].bIsRpt = TRUE;
						MEMCPY(g_stLcrData[g_u8Port].au32DataOld, au32DataTemp, 24);
						EVT_PostEvent(g_evtLcrSaveID,	NULL);
					}
				}
				else
				{
					if(g_stLcrSave[g_u8Port].u8WritePos)
					{
						g_stLcrData[g_u8Port].u8PackNum++;
						g_stLcrData[g_u8Port].bIsRpt = TRUE;
						EVT_PostEvent(g_evtLcrSaveID,	NULL);
					}
					else
					{
						DBG(DBG_D, "data is same!");
					}
				}
				g_u8LcrCmdInx = 0;
				g_stLcrData[g_u8Port].u8HandCnt = 10;
				
				if(g_u8Port == 0)
				{
					g_u8Port = 1;
					EVT_PostEvent(g_evtLcrCmdID, NULL);//取下一个数据事件
				}
			}
			else
			{
				g_u8ReSendCnt = 0;
				EVT_PostEvent(g_evtLcrCmdID, NULL);//取下一个数据事件
			}
		}

		else
		{
			DBG(DBG_I, "---lcr2 rev data-%s", au8TempStr);
			if((E_CMD_ID_MAX == g_eCmdID) && (u16DataLen <= 10))
			{
				au8InitCmd[2] = g_stLcrInfo[g_u8Port].u8DevNum;
				applcrFillCrc(au8InitCmd, 9);
				HAL_UartSendData((E_HAL_UART)(E_HAL_UART_LCR1+g_u8Port), au8InitCmd, 9);
				break;
			}
			
			switch (g_eCmdID)
			{
				case E_START_ID:
				case E_STOP_ID:
				case E_PAUSE_ID:
				case E_PRINT_ID:
				case E_MDF_LCR_ND_ID:
				case E_RE_GR_ID:
				{
					
					if(au8Temp[3] == g_stLcrInfo[0].u8DevNum)
					{
						u8Port = 0;
					}
					else
					{
						u8Port = 1;
					}
					au8InitCmd[2] = g_stLcrInfo[u8Port].u8DevNum;
					applcrFillCrc(au8InitCmd, 9);
					HAL_UartSendData((E_HAL_UART)(E_HAL_UART_LCR1+u8Port), au8InitCmd, 9);
					if(g_eCmdID-E_START_ID < 3)
					{
						g_au8LastMtrCmd[u8Port] =  g_eCmdID-E_START_ID;
					}
					TMR_Stop(g_tmrStopCmdID);
					u8DataLen = strlen((char*)apszLcrCmd[g_eCmdID-E_START_ID]);
					DBG(DBG_D, apszLcrCmd[g_eCmdID-E_START_ID]);
					applcrSendDataToAPP((MUINT8 *)apszLcrCmd[g_eCmdID-E_START_ID],  u8DataLen);
					TMR_Start(g_tmrLcrCmdID);
					if(g_eCmdID <= E_STOP_ID)
					{
						g_eDevStatus = (E_DEV_STATUS)(g_eCmdID-E_START_ID);
					}
					g_eCmdID = E_CMD_ID_MAX;
				}break;
				
				case E_STATE_ID:
				{
					u8StateInx = 0x0f&au8Temp[7] - 1;
					if(u8StateInx <= 3)
					{
						sprintf(au8SendBuf, "%s %s", apszLcrCmd[au8Temp[2]-E_STOP_ID], apszLcrSta[u8StateInx]);
						u8DataLen = STRLEN(au8SendBuf);
						DBG(DBG_D, au8SendBuf);
						applcrSendDataToAPP((MUINT8 *)au8SendBuf,  u8DataLen);
					}
				}break;
				
				case E_FIND_ADDER:
				{
					if(g_u8FindPort < 3)
					{
						TMR_Kill(g_tmrFindAddrID);
						memset(au8SendBuf, 0, 64);
						sprintf(au8SendBuf,"LxFindLcrNode %d,%d,", g_u8FindPort, au8Temp[3]);
						u8DataLen = STRLEN(au8SendBuf);
						DBG(DBG_I, au8SendBuf);
						applcrSendDataToAPP((MUINT8 *)au8SendBuf,  u8DataLen);
					}
					
					else if(g_u8FindPort == 3)
					{
						g_u8PortStatus = 0x01;
						g_u8FindPort = 4;
						g_u8FindStartAddr = 1;
						g_u8FindEndAddr = 255;
						TMR_Restart(g_tmrFindAddrID);
					}
				}break;
				
				
				default:
					break;
			}
			break;
		}
	}while(0);
	return;
}


static void applcrSendCmdEvt(void *pArg)
{
	MUINT8 au8CmdByte2[6][10]= 
	{	//-head---,-to-,from-,sta,len ,data1  0 , crc0 crc1
		{0x7E,0x7E,0x01,0x14,0x01,0x02,0x20,0x02,0x77,0xCD},  //GrossQty
		{0x7E,0x7E,0x01,0x14,0x00,0x02,0x20,0x04,0x40,0xFE},  //FlowRate
		{0x7E,0x7E,0x01,0x14,0x01,0x02,0x20,0x11,0x40,0xFE},  //17
		{0x7E,0x7E,0x01,0x14,0x00,0x02,0x20,0x12,0x40,0xFE},  //18
		{0x7E,0x7E,0x01,0x14,0x01,0x02,0x20,0x64,0x40,0xFE},  //100
		{0x7E,0x7E,0x01,0x14,0x00,0x02,0x20,0x65,0x40,0xFE}   //101
	}; 

	//au8CmdByte2[g_u8LcrCmdInx][3] += g_u8LcrCmdInx;
#if MONI
	if(g_u8Port == 0)
	{
		au8CmdByte2[g_u8LcrCmdInx][2] = g_stLcrInfo[g_u8Port].u8DevNum + 14;
	}
	else
#endif
	{
		au8CmdByte2[g_u8LcrCmdInx][2] = g_stLcrInfo[g_u8Port].u8DevNum;
	}
	applcrSendCmdToLcr(au8CmdByte2[g_u8LcrCmdInx]);
	if(g_eLcrWorkMode == E_CMD_MODE)
	{
		TMR_Restart(g_tmrLcrGetDataID);
	}
	
	return;
}


/*
***********************************************************************************************
*  功能:发送AT指令超时，重发
*
*  描述: 无
*
*  参数: 无
*
*  返回:  无
*
***********************************************************************************************
*/

static void applcrTimeOut(TMR u8Tmr, void *pArg)
{
	
	DBG(DBG_D, "---Start get data---%d---%d", g_u8Port, g_u8LcrCmdInx);

	EVT_PostEvent(g_evtLcrCmdID, NULL);
	
	return;
}


static void applcrGetDataTimeOut(TMR u8Tmr, void *pArg)
{
	if(g_stLcrData[g_u8Port].u8HandCnt > 0)//心跳计数，为0表示没接仪表
	{
		g_stLcrData[g_u8Port].u8HandCnt--;
	}

	do
	{
		if(g_u8ReSendCnt >= 2)//第一条就超时，大概率是没接设备
		{
			if(1 == g_u8Port)
			{
				g_u8Port = 0;
			}
			else
			{
				g_u8Port = 1;
			}
			
			if(g_u8LcrCmdInx != 0)//不是第一条指令超时，说明有仪表连接
			{
				g_u8ReSendCnt = 0;
			}
			
		}
		else
		{
			g_u8ReSendCnt++;
		}
	}while(0);

	EVT_PostEvent(g_evtLcrCmdID, NULL);
	
	return;
}

static void applcrRcvDataCmdCallback(MUINT8 *pu8Data, MUINT16 u16DataLen)
{
	MUINT8 au8Temp[128];
	MUINT8 au8TimeStr[11];
	MUINT8 au8SendBuf[64];
	MUINT8 u8DataLen = 0;
	MUINT8 u8Index = 0;
	MUINT8 u8LcrAddr = 0;
	MUINT32 u32Result = ERR_NONE;
	
	if(u16DataLen > 128)
	{
		DBG(DBG_D, "LCR----%d-----",u16DataLen);
		return;
	}
	
	memset(au8Temp, 0, 128);
	MEMCPY(au8Temp, pu8Data, u16DataLen);

	DBG(DBG_D, "LCR----%d----%s----",u16DataLen, au8Temp);

	if(NULL != strstr((char*)au8Temp, "SetMode"))
	{
		applcrSetLcrWorkMode((E_LCR_WORK_MODE)(au8Temp[8]-'0'));
		return;
	}
	
	else if(NULL != strstr((char*)au8Temp, "SetBtName"))
	{
		memset(au8SendBuf, 0, 64);
    	MEMCPY(au8SendBuf, "LxSetBtName 0", 13);
    	
    	UTIL_RemoveStrNewLine((MCHAR*)&au8Temp[10]);
		u32Result = APPBT_SetBtName(&au8Temp[10]);
		if(u32Result != ERR_NONE)
		{
			au8SendBuf[12] = '1';
		}
		applcrSendDataToAPP(au8SendBuf, 13);
		return;
	}

	else if(NULL != strstr((char*)au8Temp, "SetBtPwd"))
	{
		memset(au8SendBuf, 0, 64);
    	MEMCPY(au8SendBuf, "LxSetBtPwd 0", 12);
    	
    	UTIL_RemoveStrNewLine((MCHAR*)&au8Temp[9]);
		u32Result = APPBT_SetBtPwd(&au8Temp[9]);
		if(u32Result != ERR_NONE)
		{
			au8SendBuf[11] = '1';
		}
		applcrSendDataToAPP(au8SendBuf, 13);
		return;
	}

	else if(NULL != strstr((char*)au8Temp, "SetWifiName"))
	{
		memset(au8SendBuf, 0, 64);
    	MEMCPY(au8SendBuf, "LxSetWifiName 0", 15);
    
		UTIL_RemoveStrNewLine((MCHAR*)&au8Temp[12]);
		u32Result = APPEC20_SetWifiSsid(&au8Temp[12]);
		if(u32Result != ERR_NONE)
		{
			au8SendBuf[14] = '1';
		}
		applcrSendDataToAPP(au8SendBuf, 15);
		return;
	}

	else if(NULL != strstr((char*)au8Temp, "SetWifiPwd"))
	{
		memset(au8SendBuf, 0, 64);
		MEMCPY(au8SendBuf, "LxSetWifiPwd 0", 14);
		
		UTIL_RemoveStrNewLine((MCHAR*)&au8Temp[11]);
		u32Result = APPEC20_SetWifiPassword(&au8Temp[11]);
		if(u32Result != ERR_NONE)
		{
			au8SendBuf[13] = '1';
		}
		applcrSendDataToAPP(au8SendBuf, 14);
		return;
	}

	else if(NULL != strstr((char*)au8Temp, "SetServerIp"))
	{
		memset(au8SendBuf, 0, 64);
		MEMCPY(au8SendBuf, "LxSetServerIp 0", 15);
		
		UTIL_RemoveStrNewLine((MCHAR*)&au8Temp[12]);
		u32Result = APPEC20_SetServerIp(&au8Temp[12]);
		if(u32Result != ERR_NONE)
		{
			au8SendBuf[14] = '1';
		}
		applcrSendDataToAPP(au8SendBuf, 15);
		return;
	}
	
	else if(NULL != strstr((char*)au8Temp, "SetServerPort"))
	{
		memset(au8SendBuf, 0, 64);
		MEMCPY(au8SendBuf, "LxSetServerPort 0", 17);
		
		UTIL_RemoveStrNewLine((MCHAR*)&au8Temp[14]);
		u32Result = APPEC20_SetServerPort(&au8Temp[14]);
		if(u32Result != ERR_NONE)
		{
			au8SendBuf[16] = '1';
		}
		applcrSendDataToAPP(au8SendBuf, 17);
		return;
	}

	else if(NULL != strstr((char*)au8Temp, "RdBtName"))
	{
		applcrReadBtName();
		return;
	}

	else if(NULL != strstr((char*)au8Temp, "DeleteAll"))
	{
		applcrDeleteAll(pu8Data, u16DataLen);
		return;
	}

	else if(NULL != strstr((char*)au8Temp, "SetPortLcrNode"))
	{
		applcrSetPortLcrAddr(pu8Data, u16DataLen);
		return;
	}
	
	else if(NULL != strstr((char*)au8Temp, "RdPortLcrNode"))
	{
		applcrRdPortLcrNum();
		return;
	}

	else if((NULL != strstr((char*)au8Temp, "GetDataTs"))&&(u16DataLen > 20))
	{
		applcrGetDataTs(pu8Data, u16DataLen);
		return;
	}

	else if(NULL != strstr((char*)au8Temp, "GetDataEcho"))
	{
		applcrGetDataEcho(pu8Data, u16DataLen);
		return;
	}

	else if(NULL != strstr((char*)au8Temp, "GetData"))
	{
		applcrGetData(pu8Data, u16DataLen);
		return;
	}
	
	else if(NULL != strstr((char*)au8Temp, "BoxStorage"))
	{
		applcrGetBoxStorage(&au8Temp[11]);
		return;
	}
	
	else if(NULL != strstr((char*)au8Temp, "BoxStatus"))
	{
		applcrGetBoxStatus();
		return;
	}

	else if(NULL != strstr((char*)au8Temp, "BoxInfo"))
	{
		applcrGetBoxInfo();
		return;
	}

	else if(NULL != strstr((char*)au8Temp, "SetBoxTime"))
	{
		MEMCPY(au8TimeStr, &au8Temp[11], 10);
		au8Temp[10] = 0;
		applcrSetBoxTime(STR2UINT32(au8TimeStr));
		return;
	}
	 
	else if(NULL != strstr((char*)au8Temp, "BoxTime"))
	{
		applcrGetBoxTime();
		return;
	}

	else if(NULL != strstr((char*)au8Temp, "HisDataTime"))
	{
		applcrGetBoxHisDataTime(pu8Data, u16DataLen);
		return;
	}

	else if(NULL != strstr((char*)au8Temp, "BoxReset"))
	{
		memset(au8SendBuf, 0, 64);
		MEMCPY(&au8SendBuf, "LxBoxReset 0", 12);
		u8DataLen = STRLEN(au8SendBuf);
		HAL_UartSendData(E_HAL_UART_BT, (MUINT8 *)au8SendBuf,  u8DataLen);
		DBG(DBG_I, au8SendBuf);
		APPEC20_SendDataToWifi((MUINT8 *)au8SendBuf,  u8DataLen);

		u32Result = TMR_Creat(1000, applcrRebootTimeOut, NULL, &g_tmrRebootCmdID);
		ASSERT(ERR_NONE == u32Result);
		TMR_Start(g_tmrRebootCmdID);
		
		return;
	}
	
	else if(NULL != strstr((char*)au8Temp, "ReBoxParam"))
	{
		memset(au8SendBuf, 0, 64);
		MEMCPY(&au8SendBuf, "LxReBoxParam 0", 14);
		u8DataLen = STRLEN(au8SendBuf);

		APPCFG_ReDefaultBoxParam();
		
		applcrSendDataToAPP(au8SendBuf, u8DataLen);
		
		return;
	}

	else if(NULL != strstr((char*)au8Temp, "SetDbg"))
	{
		DBG_SetPrintLevel((E_DBG_LEVEL)(au8Temp[7] - '0'));
		return;
	}

	else if(NULL != strstr((char*)au8Temp, "GetLastMtrCmd"))
	{
		applcrGetLastMtrCmd(&au8Temp[14]);
		return;
	}

	else if(NULL != strstr((char*)au8Temp, "GetLastCmd"))
	{
		applcrGetLastCmd(&au8Temp[11]);
		return;
	}
	
	if(g_eLcrWorkMode == E_BRIDGE_MODE)
	{
		DBG(DBG_I, "u16DataLen = %d", u16DataLen);
		if(au8Temp[u16DataLen - 1] == 0x0a && au8Temp[u16DataLen - 2] == 0x0d)
		{
			u16DataLen -= 2;
		}
		
		if(au8Temp[2] == g_stLcrInfo[0].u8DevNum)
		{
			HAL_UartSendData(E_HAL_UART_LCR1, pu8Data, u16DataLen);
		}
		
		else if(au8Temp[2] == g_stLcrInfo[1].u8DevNum)
		{
			HAL_UartSendData(E_HAL_UART_LCR2, pu8Data, u16DataLen);
		}
		return;
	}
	
	else//CMD_MODE
	{
		if(NULL != strstr((char*)au8Temp, "Stop"))
		{
			g_u8ReSendCmdCnt = 0;
			for(u8Index = 5;  u8Index < u16DataLen; u8Index++)
			{
				if(au8Temp[u8Index] != ',')
				{
					u8LcrAddr = 10*u8LcrAddr + (au8Temp[u8Index] - '0');
				}
				else
				{
					break;
				}
			}
			applcrStartOrStopCmd(u8LcrAddr, E_STOP_ID);
			return;
		}
		else if(NULL != strstr((char*)au8Temp, "Start"))
		{
			g_u8ReSendCmdCnt = 0;
	
			for(u8Index = 6;  u8Index < u16DataLen; u8Index++)
			{
				if(au8Temp[u8Index] != ',')
				{
					u8LcrAddr = 10*u8LcrAddr + (au8Temp[u8Index] - '0');
				}
				else
				{
					break;
				}
			}
			applcrStartOrStopCmd(u8LcrAddr, E_START_ID);
			return;
		}

		else if(NULL != strstr((char*)au8Temp, "Pause"))
		{
			g_u8ReSendCmdCnt = 0;
	
			for(u8Index = 6;  u8Index < u16DataLen; u8Index++)
			{
				if(au8Temp[u8Index] != ',')
				{
					u8LcrAddr = 10*u8LcrAddr + (au8Temp[u8Index] - '0');
				}
				else
				{
					break;
				}
			}
			applcrStartOrStopCmd(u8LcrAddr, E_PAUSE_ID);
			return;
		}

		else if(NULL != strstr((char*)au8Temp, "Print"))
		{
			g_u8ReSendCmdCnt = 0;
			applcrPrintCmd(pu8Data, u16DataLen);
			return;
		}

		else if(NULL != strstr((char*)au8Temp, "SwitchState"))
		{
			applcrGetSwitchStateCmd(pu8Data, u16DataLen);
			return;
		}
			
		else if(NULL != strstr((char*)au8Temp, "GetLcrNode"))
		{
			applcrGetLcrNode(pu8Data, u16DataLen);
			return;
		}

		else if(NULL != strstr((char*)au8Temp, "RdRegister"))
		{
			applcrRdRegister();
			return;
		}
		else if(NULL != strstr((char*)au8Temp, "ModifyLcrNode"))
		{
			applcrModifyLcrNodeCmd(pu8Data, u16DataLen);
			return;
		}

		else if(NULL != strstr((char*)au8Temp, "PresetGross"))
		{
			g_u8ReSendCmdCnt = 0;
			applcrPresetGrossCmd(pu8Data, u16DataLen);
			return;
		}
	}
	return;
}

static void applcr11DataEvtProcess(MUINT8 *pu8Data, MUINT16 u16DataLen)
{
	MUINT8 au8TempStr[128];
	
	memset(au8TempStr, 0, 128);
	UTIL_DataToHexString(au8TempStr, 128, pu8Data, u16DataLen);
	DBG(DBG_I, "---lcr1 rev data-%s", au8TempStr);
	HAL_UartSendData(E_HAL_UART_LCR2, pu8Data, u16DataLen);
	
	return;
}


static void applcr22DataEvtProcess(MUINT8 *pu8Data, MUINT16 u16DataLen)
{
	MUINT8 au8TempStr[128];
	
	memset(au8TempStr, 0, 128);
	UTIL_DataToHexString(au8TempStr, 128, pu8Data, u16DataLen);
	DBG(DBG_I, "---lcr2 rev data-%s", au8TempStr);
	
	HAL_UartSendData(E_HAL_UART_LCR1, pu8Data, u16DataLen);
	return;
}

/*
***********************************************************************************************
*  功能:模块初始化
*
*  描述: 无
*
*  参数: 无
*
*  返回:  无
*
***********************************************************************************************
*/
void APPLCR_Init(void)
{
	MUINT8  u8Port = 0;
	MUINT8  u8Index = 0;
	MUINT8 au8DataBuf[64];
	MUINT32 u32Address = 0;
	MUINT32 u32RdAddr = 0;
	MUINT32 u32TimeTemp = 0;
	MUINT32 u32TimeEnd = 0;
	MUINT32 u32Second = 0;
	MUINT32 u32Result = ERR_NONE;
	T_BOX_PARAM stBoxParm;
	 
	HAL_UartSetBaudRate(E_HAL_UART_LCR1, E_HAL_UART_BAUD_19200, E_HAL_UART_WORD_8, E_HAL_UART_PARITY_NO);
    HAL_UartSetPackInterval(E_HAL_UART_LCR1, 50);
    HAL_UartSetCallback(E_HAL_UART_LCR1, applcrDataEvtProcess, NULL);
 
	HAL_UartSetBaudRate(E_HAL_UART_LCR2, E_HAL_UART_BAUD_19200, E_HAL_UART_WORD_8, E_HAL_UART_PARITY_NO);
	HAL_UartSetPackInterval(E_HAL_UART_LCR2, 50);
	HAL_UartSetCallback(E_HAL_UART_LCR2, applcrDataEvtProcess, NULL);

	u32Result = EVT_Creat(applcrSendCmdEvt, &g_evtLcrCmdID); 
	ASSERT(ERR_NONE == u32Result);

	u32Result = EVT_Creat(applcrSaveDataEvt, &g_evtLcrSaveID); 
	ASSERT(ERR_NONE == u32Result);

	u32Result = EVT_Creat(applcrSendHisDataToAppEvt, &g_evtSendHisAppID);
	ASSERT(ERR_NONE == u32Result);
	
	u32Result = TMR_CreatRepeatTimer(1000, applcrTimeOut, NULL, &g_tmrLcrCmdID);
    ASSERT(ERR_NONE == u32Result);

	u32Result = TMR_Creat(120, applcrGetDataTimeOut, NULL, &g_tmrLcrGetDataID);
    ASSERT(ERR_NONE == u32Result);

	u32Result = TMR_Creat(120, applcrStopCmdTimeOut, NULL, &g_tmrStopCmdID);
	ASSERT(ERR_NONE == u32Result);

	u32Result = TMR_CreatRepeatTimer(1000, applcrLinkCheckTimeOut, NULL, &g_tmrLinkCheckID);
	ASSERT(ERR_NONE == u32Result);
	TMR_Start(g_tmrLinkCheckID);
	
	//读取表头信息
	memset(&g_stLcrInfo, 0, sizeof(g_stLcrInfo));
	memset(&g_stLcrData, 0, sizeof(g_stLcrData));
	memset(&g_stLcrSave, 0, sizeof(g_stLcrSave));
	
	for(u8Port = 0; u8Port < 2; u8Port++)
	{
		u32Address = u8Port*LCR_START_ADDR2;
		gd25q256df_read_data((MUINT8 *)&g_stLcrInfo[u8Port], u32Address, sizeof(T_LCR_DATA_INFO));

		if(g_stLcrInfo[u8Port].u32Flag != 0x5a5a5a5a)
		{
			memset(&g_stLcrInfo, 0, sizeof(g_stLcrInfo));
		}
		else
		{
/*
			DBG(DBG_D, "[%d].u32WrAddr = %x", u8Port, g_stLcrSave[u8Port].u32WrAddr);
			DBG(DBG_D, "[%d].u32EndAddr = %x", u8Port, g_stLcrInfo[u8Port].u32EndAddr);
			DBG(DBG_D, "[%d].u32StartAddr = %x", u8Port, g_stLcrInfo[u8Port].u32StartAddr);
			DBG(DBG_D, "[%d].u8DevNum = %x", u8Port, g_stLcrInfo[u8Port].u8DevNum);
			DBG(DBG_D, "[%d].u32DataCnt = %d", u8Port, g_stLcrInfo[u8Port].u32DataCnt);
			DBG(DBG_D, "[%d].u32RdAddr = %X", u8Port, g_stLcrInfo[u8Port].u32RdAddr);
			DBG(DBG_D, "-------------------------------------");
*/
			if(g_stLcrInfo[u8Port].u32StartAddr == 0xffffffff)
			{
				g_stLcrInfo[u8Port].u32StartAddr = 0;
				g_stLcrInfo[u8Port].u32EndAddr = 0;
				g_stLcrInfo[u8Port].u32DataCnt = 0;
				g_stLcrInfo[u8Port].u32RdAddr = 0;
			}
			
			else if(g_stLcrInfo[u8Port].u32DataCnt < (261910 - 64))
			{
				u32RdAddr = g_stLcrInfo[u8Port].u32EndAddr + u8Port*LCR_START_ADDR2 + SECTOR_BYTE;
				u32RdAddr &= 0xfffff000;
				for(u8Index = 0;  u8Index < 64; u8Index++)
				{
					gd25q256df_read_data(au8DataBuf, u32RdAddr, 4);
					MEMCPY(&u32TimeTemp, au8DataBuf, 4);
					if(u32TimeTemp == 0xffffffff)
					{
						break;
					}
					else if(u32TimeTemp > u32TimeEnd)
					{
						u32TimeEnd = u32TimeTemp;
					}
					u32RdAddr += 64;
				}

				g_stLcrInfo[u8Port].u32EndAddr = u32RdAddr - u8Port*LCR_START_ADDR2 - SECTOR_BYTE;
				
				if(g_stLcrInfo[u8Port].u32EndAddr >= g_stLcrInfo[u8Port].u32StartAddr)
				{
					g_stLcrInfo[u8Port].u32DataCnt = (u32RdAddr - u8Port*LCR_START_ADDR2 - SECTOR_BYTE)/64;
				}
				else
				{
					g_stLcrInfo[u8Port].u32DataCnt = (LCR_FLASH_TOTAL/2 - SECTOR_BYTE)/64;
					g_stLcrInfo[u8Port].u32DataCnt -= 64;//擦除了一个扇区
					g_stLcrInfo[u8Port].u32DataCnt += ((g_stLcrInfo[u8Port].u32EndAddr%4096)/64);
				}
				g_stLcrSave[u8Port].u32WrAddr = g_stLcrInfo[u8Port].u32EndAddr;
			}
		}
	
		DBG(DBG_D, "[%d].u32WrAddr = %x", u8Port, g_stLcrSave[u8Port].u32WrAddr);
		DBG(DBG_D, "[%d].u32EndAddr = %x", u8Port, g_stLcrInfo[u8Port].u32EndAddr);
		DBG(DBG_D, "[%d].u32StartAddr = %x", u8Port, g_stLcrInfo[u8Port].u32StartAddr);
		DBG(DBG_D, "[%d].u8DevNum = %x", u8Port, g_stLcrInfo[u8Port].u8DevNum);
		DBG(DBG_D, "[%d].u32DataCnt = %d", u8Port, g_stLcrInfo[u8Port].u32DataCnt);
		DBG(DBG_D, "[%d].u32RdAddr = %X", u8Port, g_stLcrInfo[u8Port].u32RdAddr);
		g_stLcrInfo[u8Port].u32RdAddr = 0;
	
	}

    DBG(DBG_I, "APPLCR_Init OK!");

	APPEC20_SetRevDataProssCB(applcrRcvDataCmdCallback);

	APPEC20_SetSendDoneProssCB(applcrWifiSendDoneCallback);

	u32Second = RTC_GetDateTimeToSec();
	DBG(DBG_D, "SysTime Second = %d", u32Second);
	if(u32TimeEnd > u32Second)
	{
		RTC_SetDateTime(u32TimeEnd);
	}

	stBoxParm = APPCFG_GetBoxParam();
	if(stBoxParm.eLcrWorkMode == E_CMD_MODE)
	{
		applcrSetLcrWorkMode(stBoxParm.eLcrWorkMode);
	}

	HAL_UartSendData(E_HAL_UART_LCR1, "TEST LCR1!", 10);
	HAL_DelayMs(20);
	HAL_UartSendData(E_HAL_UART_LCR2, "TEST LCR2!", 10);
	
	//DBG(DBG_I, "ADC = %d!", HAL_AdcGetValue(E_HAL_ADC_VCC12));

	applcrGetBoxInfo();
	
	return;
}


