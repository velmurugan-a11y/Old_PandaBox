/*
*********************************************************************************************************
*
*  文件: app_ec20.c
*  作者: 徐文杰
*  版本: V1.0.0
*  日期: 2023-09-01
*  描述: EC20模块组
*
********************************************************************************************************
*/

#include "app_ec20.h"
#include <string.h>
#include <stdio.h>

#define CMD_CNT_MAX   15
											
// 网络角色字符串

static MUINT8 * const g_apszAtCmd[CMD_CNT_MAX] = {"ATE0","QGMR","CGSN","CIMI","QWSSID=TBOX_APP", "QWAUTH=5,4,\"123456789\"",\
												"QWIFI=1","QWTOCLIEN=1,5553","CSQ","QIACT=1",\
												"QICSGP=1,1,\"MOBILE\",\"\",\"\",1",\
												"QIOPEN",\
												"QGPS=1","QGPSCFG=\"nmeasrc\",1", "QDATAFWDHEX=1"};//,"AT+QDATAFWD=0,3,12,\"313233343536\",1"};
/*
static MUINT8 * const g_apszAtCmd[CMD_CNT_MAX] = {"ATE0","QWSSID=TBOX_APP", "QWAUTH=5,4,\"123456789\"",\
												"QWIFI=1","QWTOCLIEN=1,5555","QIACT=1","CIMI",\
												"QGPS=1","QGPSCFG=\"nmeasrc\",1","CGSN=1"};//,"AT+QDATAFWD=0,3,12,\"313233343536\",1"};
*/
static MCHAR * const g_apszAtCmdAck[CMD_CNT_MAX] = {"OK","OK", "OK", "OK", "OK", "OK", "OK","OK","OK","OK", "OK", "OK","OK","OK","OK"};

static MUINT8 g_u8SendInx = 0;
TMR g_tmrEc20CmdID;
TMR g_tmrHeartID;
TMR g_tmrCsqID;

static EVT g_evtEc20CmdID;

static T_GPS_DATA g_stGpsData;

static MBOOL g_bWifiFlag = FALSE;

static MBOOL g_bWifiLinkFlag = FALSE;

HAL_UART_RCV_CB g_pfnRcvCB = NULL;

HAL_UART_RCV_CB g_pfnWifiSnDoneCB;

static MUINT8 g_u8Csq = 0;

static MUINT8 g_au8ImeiCode[16];

static MUINT8 g_au8CimiCode[16];//460071301117884

static MBOOL g_bIsHaveSim = FALSE;

static MUINT8 g_u8HeardCnt = 0;

static MBOOL g_bUpdateFlag = FALSE;

static MUINT32 g_u32HeartTime = HEART_TIME;

E_TCP_STATUS g_eTcpStatus = E_TCP_CLOSING;

E_EC20_CUR_CMD g_eEc20CurCmd = E_EC20_IDLE;


/*
***********************************************************************************************
*  功能:WIFI闪灯定时器
*
*  描述: 无
*
*  参数: 无
*
*  返回:  无
*
***********************************************************************************************
*/
/*
static void appec20UpdateBin(void)
{
	T_UPDATE_APP_INFO stAppInfo;
	
	UPDATE_EraseAppInfo();
	HAL_DelayMs(100);
	UPDATE_AppInfoWrite(&stAppInfo);

	UPDATE_ReceiverInit(E_UPDATE_TYPE_BY_OMS_RCV);
}

/*
***************************************************************************************
*  功能:发送AT指令超时，重发
*
*  描述: 无
*
*  参数: 无
*
*  返回:  无
*
*****************************************************************************************
*/
static void appec20ReSendCmdTimeOut(TMR u8Tmr, void *pArg)
{
	if(g_u8SendInx < CMD_CNT_MAX)
	{
		EVT_PostEvent(g_evtEc20CmdID, NULL);
	}
	return;
}



/*
***********************************************************************************************
*  功能:4G与服务器心跳包定时器
*
*  描述: 无
*
*  参数: 无
*
*  返回:  无
*
***********************************************************************************************
*/
static void appec20HeartTimeOut(TMR u8Tmr, void *pArg)
{
	MUINT8 au8AtCmdBuf[128] = {0};
	static MBOOL bSendHeart = FALSE;
	MUINT16 u16DataLen = 0;
	T_BOX_PARAM stBoxParam;

	memset(au8AtCmdBuf, 0, 128);

	if(g_eTcpStatus == E_TCP_CONNECTED)
	{
		if(bSendHeart == FALSE)
		{
			sprintf((char*)au8AtCmdBuf , "AT+QISTATE?\r\n");
			u16DataLen = STRLEN((char*)au8AtCmdBuf); 
			au8AtCmdBuf[u16DataLen] = 0;
			HAL_UartSendData(E_HAL_UART_EC20, au8AtCmdBuf, u16DataLen);
			DBG(DBG_I, "---check server status---");
			g_eEc20CurCmd = E_EC20_QISTATE;
			if(g_u32HeartTime != HEART_TIME)
			{
				g_u32HeartTime = HEART_TIME;
				TMR_Stop(g_tmrHeartID);
				TMR_SetPeriod(g_tmrHeartID, HEART_TIME);
				TMR_Restart(g_tmrHeartID);
			}
			bSendHeart = TRUE;
			g_u8HeardCnt++;
		}
		else
		{
			APPEC20_SendDataToServer("Heart", 5);
			bSendHeart = FALSE;
		}
	}
	
	else if(g_eTcpStatus == E_TCP_CLOSING && g_u8HeardCnt >= 3)
	{
		if(g_u8HeardCnt == 3)
		{
			sprintf((char*)au8AtCmdBuf, "AT+QICLOSE=0\r\n");
			g_eEc20CurCmd = E_EC20_QICLOSE;
			g_u8HeardCnt = 4;
			g_u32HeartTime = HEART_TIME/5;
			TMR_Stop(g_tmrHeartID);
			TMR_SetPeriod(g_tmrHeartID, g_u32HeartTime);
			TMR_Restart(g_tmrHeartID);
		}
		else
		{
			stBoxParam = APPCFG_GetBoxParam();
			if(UTIL_IsValidIp(stBoxParam.au8ServerIp))
			{
				sprintf((char*)au8AtCmdBuf, "AT+QIOPEN=1,0,\"TCP\",\"%s\",%s,0,0\r\n", stBoxParam.au8ServerIp, stBoxParam.au8ServerPort);
			}
			else
			{
				sprintf((char*)au8AtCmdBuf, "AT+QIOPEN=1,0,\"TCP\",\"%s\",%s,0,0\r\n", SERVER_IP, SERVER_PORT);
			}
			g_eTcpStatus = E_TCP_OPENING;
			g_eEc20CurCmd = E_EC20_QIOPEN;
			g_u32HeartTime = HEART_TIME;
			TMR_Stop(g_tmrHeartID);
			TMR_SetPeriod(g_tmrHeartID, g_u32HeartTime);
			TMR_Restart(g_tmrHeartID);
		}
		u16DataLen = STRLEN((char*)au8AtCmdBuf); 
		au8AtCmdBuf[u16DataLen] = 0;
		HAL_UartSendData(E_HAL_UART_EC20, au8AtCmdBuf, u16DataLen);
		DBG(DBG_I, "reconect server , %s", au8AtCmdBuf);
	}
	return;
}

/*
***********************************************************************************************
*  功能:4G与服务器心跳包定时器
*
*  描述: 无
*
*  参数: 无
*
*  返回:  无
*
***********************************************************************************************
*/
static void appec20CheckCsqTimeOut(TMR u8Tmr, void *pArg)
{
	HAL_UartSendData(E_HAL_UART_EC20, "AT+CSQ\r\n", 8);

	return;
}

/*
***********************************************************************************************
*  功能:发送AT指令事件
*
*  描述: 无
*
*  参数: 无
*
*  返回:  无
*
***********************************************************************************************
*/
static void appec20SendCmdEvt(void *pArg)
{
	MUINT8 u8AtCmdBuf[128];
	MUINT32 u32DataLen = 0;
	T_BOX_PARAM stBoxParam;

	stBoxParam = APPCFG_GetBoxParam();
	memset(u8AtCmdBuf, 0, 128);

	if((NULL != strstr((char*)g_apszAtCmd[g_u8SendInx], "QIOPEN")) && (g_bIsHaveSim == FALSE))
	{
		g_u8SendInx++;
		DBG(DBG_D, "No sim, jump this cmd !");
	}
	
	if(g_u8SendInx < CMD_CNT_MAX)
	{
		u32DataLen = STRLEN((char*)g_apszAtCmd[g_u8SendInx]); 
		if(0 == g_u8SendInx)
		{
			sprintf((char*)u8AtCmdBuf, "%s\r\n", g_apszAtCmd[g_u8SendInx]);
		}
		else if((NULL != strstr((char*)g_apszAtCmd[g_u8SendInx], "QWSSID")) && (0x5a == stBoxParam.u8WifiSsidFlag))//SET WIFI NAME
		{
			sprintf((char*)u8AtCmdBuf , "AT+QWSSID=%s\r\n", stBoxParam.au8WifiSsid);
		}
		else if((NULL != strstr((char*)g_apszAtCmd[g_u8SendInx], "QWAUTH")) && (0x5a == stBoxParam.u8WifiPwdFlag))//SET WIFI PASSWORD
		{
			sprintf((char*)u8AtCmdBuf , "AT+QWAUTH=5,4,\"%s\"\r\n", stBoxParam.au8WifiPwd);
		}
		else if(NULL != strstr((char*)g_apszAtCmd[g_u8SendInx], "QIOPEN"))
		{
			if(UTIL_IsValidIp(stBoxParam.au8ServerIp))
			{
				sprintf((char*)u8AtCmdBuf , "AT+QIOPEN=1,0,\"TCP\",\"%s\",%s,0,0\r\n", stBoxParam.au8ServerIp, stBoxParam.au8ServerPort);
			}
			else
			{
				sprintf((char*)u8AtCmdBuf , "AT+QIOPEN=1,0,\"TCP\",\"%s\",%s,0,0\r\n", SERVER_IP, SERVER_PORT);
			}
		}
		else if(NULL != strstr((char*)g_apszAtCmd[g_u8SendInx], "QICSGP"))
		{
			sprintf((char*)u8AtCmdBuf , "AT+QICSGP=1,1,\"%s\",\"\",\"\",1\r\n", stBoxParam.au8Apn);
		}
		
		else
		{
			sprintf((char*)u8AtCmdBuf, "AT+%s\r\n", g_apszAtCmd[g_u8SendInx]);
		}

		if(NULL != strstr((char*)g_apszAtCmd[g_u8SendInx], "CIMI"))
		{
			g_eEc20CurCmd = E_EC20_CIMI;
		}

		else if(NULL != strstr((char*)g_apszAtCmd[g_u8SendInx], "CGSN"))
		{
			g_eEc20CurCmd = E_EC20_CGSN;
		}

		else if(NULL != strstr((char*)g_apszAtCmd[g_u8SendInx], "CSQ"))
		{
			g_eEc20CurCmd = E_EC20_CSQ;
		}
		u32DataLen = STRLEN((char*)u8AtCmdBuf); 
		u8AtCmdBuf[u32DataLen] = 0;
		HAL_UartSendData(E_HAL_UART_EC20, u8AtCmdBuf, u32DataLen);
		DBG(DBG_I, "Send to EC20, %s---%d", u8AtCmdBuf, u32DataLen);
	}
	else
	{
		TMR_Stop(g_tmrEc20CmdID);
		HAL_UartSetCallback(E_HAL_UART_EC20, appec20RcvDataEvtProcess, NULL);

		if(g_eTcpStatus == E_TCP_CONNECTED)
		{
			MEMCPY(u8AtCmdBuf, "write(IMEI,", 11);
			MEMCPY(&u8AtCmdBuf[11], g_au8ImeiCode, 15);
			u8AtCmdBuf[26] = ')';
			u8AtCmdBuf[27] = 0;
			APPEC20_SendDataToServer(u8AtCmdBuf, 27);
			LED_Play(eLED_ID_4G, m_eLED_PLAY_ON); 
		}
		else
		{
			DBG(DBG_D, "---EC25 Init End !---");
		}
	}
	return;
	
}

/*
***********************************************************************************************
*  功能:发送获取GPS数据AT指令
*
*  描述: 无
*
*  参数: 无
*
*  返回:   无
*
***********************************************************************************************
*/
void APPEC20_SendGpsCmd(void)
{
	MUINT8 u8DataLen = 0;
	
	MUINT8 au8GetGpsCmd[]="AT+QGPSGNMEA=\"RMC\"\r\n";

	if(g_u8SendInx > 8)
	{
		u8DataLen = strlen((char*)au8GetGpsCmd);
		HAL_UartSendData(E_HAL_UART_EC20, au8GetGpsCmd, u8DataLen);
	}
	return;
}

void APPEC20_GetImeiCode(MCHAR *pu8ImeiCode)
{
	MEMCPY(pu8ImeiCode, g_au8ImeiCode, 15);
	return;
}

/*
***********************************************************************************************
*  功能:获取GPS数据
*
*  描述:无
*
*  参数: pstGpsData数据结构体指针
*
*  返回:   ERR_NONE成功
*
***********************************************************************************************
*/
MUINT32 APPEC20_GetGpsData(T_GPS_DATA *pstGpsData)
{
	MUINT32 u32Result = ERR_NONE;

	if(g_stGpsData.u32Time == 0 && g_stGpsData.u32Lati == 0)
	{
		u32Result = ERR_LEN_IS_ZERO;
	}
	else
	{
		MEMCPY(pstGpsData, &g_stGpsData, sizeof(g_stGpsData));
	}
	return u32Result;
}


void APPEC20_OpenWifiSend(MBOOL bIsSendWifi)
{
	g_bWifiFlag = bIsSendWifi;
	return;
}

/*
***********************************************************************************************
*  功能:通过WIFI向APP发送数据函数
*
*  描述:无
*
*  参数: pu8Data数据指针，u32DataLen数据长度
*
*  返回:  无
*
***********************************************************************************************
*/
MUINT32 APPEC20_SendDataToWifi(MUINT8 *pu8Data, MUINT16 u16DataLen)
{
	MUINT8  au8WifiTemp[UART_SND_BUF_SIZE];
	MUINT8  au8SendTemp[UART_SND_BUF_SIZE];
	MUINT16  u16SendLen = 0;  
	MUINT32 u32Result = ERR_NONE;

	do
	{
		if((FALSE == g_bWifiFlag) || (FALSE == g_bWifiLinkFlag))
		{
			//DBG(DBG_D, "wifi is disconnect g_bWifiFlag = %d, g_bWifiLinkFlag = %d!", g_bWifiFlag, g_bWifiLinkFlag);
			u32Result = ERR_WIFI_DISCONNECT;
			break;
		}
		memset(au8SendTemp, 0, UART_SND_BUF_SIZE);
		MEMCPY(au8SendTemp, pu8Data, u16DataLen);
		
		if(au8SendTemp[u16DataLen-1] != ';')
		{
			au8SendTemp[u16DataLen++] = 0x0d;
			au8SendTemp[u16DataLen++] = 0x0a;
		}
		
		memset(au8WifiTemp, 0, UART_SND_BUF_SIZE);
		sprintf((char*)au8WifiTemp, "AT+QDATAFWD=0,3,%d,", 2*u16DataLen);
		u16SendLen = strlen((char*)au8WifiTemp); 
		
		au8WifiTemp[u16SendLen++] = '"';
		UTIL_DataToHexSendString((MCHAR*)&au8WifiTemp[u16SendLen], UART_SND_BUF_SIZE, au8SendTemp, u16DataLen);
		u16SendLen += 2*u16DataLen; 
		au8WifiTemp[u16SendLen++] = '"';

		MEMCPY(&au8WifiTemp[u16SendLen], ",1\r\n", 4);
		u16SendLen += 4; 

		HAL_UartSendData(E_HAL_UART_EC20, au8WifiTemp, u16SendLen);
		DBG(DBG_I, "send to wifi len = %d,--,%s", u16SendLen, au8WifiTemp);
		g_bWifiFlag = FALSE;
		
	}while(0);
	
	return u32Result;
}


/*
***********************************************************************************************
*  功能:通过4G向服务器发送数据函数
*
*  描述:无
*
*  参数: pu8Data数据指针，u32DataLen数据长度
*
*  返回:  无
*
***********************************************************************************************
*/
MUINT32 APPEC20_SendDataToServer(MUINT8 *pu8Data, MUINT32 u32DataLen)
{
	MUINT8  au8ServerTemp[512];
	MUINT8  au8SendTemp[256];
	MUINT16  u16DataLen = 0;  
	MUINT32 u32Result = ERR_NONE;

	do
	{
		memset(au8SendTemp, 0, 256);
		
		MEMCPY(au8SendTemp, pu8Data, u32DataLen);
		if(au8SendTemp[0] != 'w' || au8SendTemp[1] != 'r')
		{
			if(au8SendTemp[u32DataLen-1] != ',')
			{
				au8SendTemp[u32DataLen++] = ',';
			}
			MEMCPY(&au8SendTemp[u32DataLen], g_au8ImeiCode, 15);
			u32DataLen += 15;
		}
		memset(au8ServerTemp, 0, 512);
		sprintf((char*)au8ServerTemp, "AT+QISEND=0,%d\r\n", u32DataLen);
		u16DataLen = strlen((char*)au8ServerTemp); 
		HAL_UartSendData(E_HAL_UART_EC20, au8ServerTemp, u16DataLen);
		HAL_DelayMs(20);
		
		HAL_UartSendData(E_HAL_UART_EC20, au8SendTemp, u32DataLen);
		HAL_DelayMs(20);
		DBG(DBG_I, "send to wifi len = %d,--,%s", u32DataLen, au8SendTemp);
		
	}while(0);
	
	return u32Result;
}

/*
***********************************************************************************************
*  功能:模块收到APP下发数据回调函数设置
*
*  描述: 无
*
*  参数: pfnRcvCB回调数据指针
*
*  返回:  无
*
***********************************************************************************************
*/
void APPEC20_SetRevDataProssCB(HAL_UART_RCV_CB pfnRcvCB)
{
	g_pfnRcvCB = pfnRcvCB;

	return;
}

/*
***********************************************************************************************
*  功能:模块收到APP下发数据回调函数设置
*
*  描述: 无
*
*  参数: pfnRcvCB回调数据指针
*
*  返回:  无
*
***********************************************************************************************
*/
void APPEC20_SetSendDoneProssCB(HAL_UART_RCV_CB pfnRcvCB)
{
	g_pfnWifiSnDoneCB = pfnRcvCB;

	return;
}

/*
***********************************************************************************************
*  功能:设置WIFI密码
*
*  描述: 无
*
*  参数: pszStrPassWd WIFI密码字符串
*
*  返回:  无
*
***********************************************************************************************
*/
MUINT32 APPEC20_SetWifiPassword(MUINT8 *pszStrPassWd)
{
	//"AT+QWAUTH=5,4,\"123456789\"",
	MUINT8 u8DataLen = 0;
	MUINT32 u32Result = ERR_NONE;
	T_BOX_PARAM stBoxParam;

	do
	{
		u8DataLen = STRLEN(pszStrPassWd);
		if(u8DataLen > 15 || u8DataLen < 8)
		{
			u32Result = ERR_LEN_OVERFLOW;
			break;
		}
		stBoxParam = APPCFG_GetBoxParam();
		MEMCPY(stBoxParam.au8WifiPwd, pszStrPassWd, u8DataLen);
		stBoxParam.au8WifiPwd[u8DataLen] = 0;
		stBoxParam.u8WifiPwdFlag = 0x5a;
		APPCFG_SetBoxParam(&stBoxParam);
	}while(0);

	return u32Result;
}


/*
***********************************************************************************************
*  功能:设置WIFI名称
*
*  描述: 无
*
*  参数: pszStrSsid WIFI名称字符串
*
*  返回:  无
*
***********************************************************************************************
*/
MUINT32 APPEC20_SetWifiSsid(MUINT8 *pszStrSsid)
{
	//"AT+QWSSID=TBOX_APP"
	MUINT8 u8DataLen = 0;
	MUINT32 u32Result = ERR_NONE;
	T_BOX_PARAM stBoxParam;

	do
	{
		u8DataLen = STRLEN(pszStrSsid);
		if(u8DataLen > 15)
		{
			u32Result = ERR_LEN_OVERFLOW;
			break;
		}
		u8DataLen = strlen((MCHAR*)pszStrSsid);
		stBoxParam = APPCFG_GetBoxParam();
		MEMCPY(stBoxParam.au8WifiSsid, pszStrSsid, u8DataLen);
		stBoxParam.au8WifiSsid[u8DataLen] = 0;
		stBoxParam.u8WifiSsidFlag = 0x5a;
		APPCFG_SetBoxParam(&stBoxParam);
	}while(0);

return u32Result;
}


/*
***********************************************************************************************
*  功能:设置服务器IP
*
*  描述: 无
*
*  参数: pszStrSsid WIFI名称字符串
*
*  返回:  无
*
***********************************************************************************************
*/
MUINT32 APPEC20_SetServerIp(MUINT8 *pszStrIp)
{
	//"SetServerIp 192.168.1.123,"
	MUINT8 u8DataLen = 0;
	MUINT8 u8Index = 0;
	MUINT8 au8RevBuf[32];
	MUINT32 u32Result = ERR_NONE;
	T_BOX_PARAM stBoxParam;

	do
	{
		u8DataLen = STRLEN(pszStrIp);
		if(u8DataLen < 10 || u8DataLen > 20)
		{
			u32Result = ERR_LEN_UNDERFLOW;
			break;
		}

		u8DataLen = strlen((MCHAR*)pszStrIp);
		MEMCPY(au8RevBuf, pszStrIp, u8DataLen);
		
		if(au8RevBuf[u8DataLen-1] == ',')
		{
			au8RevBuf[u8DataLen-1] = 0;
		}
		else
		{
			au8RevBuf[u8DataLen++] = 0;
		}
		DBG(DBG_D, "Set server ip: %s", au8RevBuf);
		stBoxParam = APPCFG_GetBoxParam();
		MEMCPY(stBoxParam.au8ServerIp, &au8RevBuf[u8Index] , u8DataLen-u8Index);
		stBoxParam.au8ServerIp[u8DataLen-u8Index] = 0;
		APPCFG_SetBoxParam(&stBoxParam);
	}while(0);

	return u32Result;
}



/*
***********************************************************************************************
*  功能:设置服务器IP
*
*  描述: 无
*
*  参数: pszStrSsid WIFI名称字符串
*
*  返回:  无
*
***********************************************************************************************
*/
MUINT32 APPEC20_SetServerPort(MUINT8 *pszStrPort)
{
	//"SetServerPort 659830"
	MUINT8 u8DataLen = 0;
	MUINT8 u8Index = 0;
	MUINT8 au8RevBuf[32];
	MUINT32 u32Result = ERR_NONE;
	T_BOX_PARAM stBoxParam;

	do
	{
		u8DataLen = STRLEN(pszStrPort);
		if(u8DataLen < 2 || u8DataLen > 6)
		{
			u32Result = ERR_LEN_UNDERFLOW;
			break;
		}

		u8DataLen = strlen((MCHAR*)pszStrPort);
		MEMCPY(au8RevBuf, pszStrPort, u8DataLen);
		if(au8RevBuf[u8DataLen-1] == ',')
		{
			au8RevBuf[u8DataLen-1] = 0;
		}
		else
		{
			au8RevBuf[u8DataLen++] = 0;
		}
		DBG(DBG_D, "Set server port: %s", au8RevBuf);
		stBoxParam = APPCFG_GetBoxParam();
		MEMCPY(stBoxParam.au8ServerPort, &au8RevBuf[u8Index] , u8DataLen-u8Index);
		stBoxParam.au8ServerPort[u8DataLen-u8Index] = 0;
		APPCFG_SetBoxParam(&stBoxParam);
	}while(0);
	
	return u32Result;
}



/*
***********************************************************************************************
*  功能:设置4G网络APN
*
*  描述: 无
*
*  参数: pszStrSsid WIFI名称字符串
*
*  返回:  无
*
***********************************************************************************************
*/
MUINT32 APPEC20_Set4GApn(MUINT8 *pszStrApn)
{
	MUINT8 u8DataLen = 0;
	MUINT32 u32Result = ERR_NONE;
	T_BOX_PARAM stBoxParam;

	do
	{
		u8DataLen = STRLEN(pszStrApn);
		if(u8DataLen > 15)
		{
			u32Result = ERR_LEN_OVERFLOW;
			break;
		}
		u8DataLen = strlen((MCHAR*)pszStrApn);
		stBoxParam = APPCFG_GetBoxParam();
		MEMCPY(stBoxParam.au8Apn, pszStrApn, u8DataLen);
		if(stBoxParam.au8Apn[u8DataLen-1] == ',')
		{
			u8DataLen--;
		}
		stBoxParam.au8Apn[u8DataLen] = 0;
		APPCFG_SetBoxParam(&stBoxParam);
	}while(0);

return u32Result;
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
void APPEC20_Init(void)
{
	MUINT32 u32Result = ERR_NONE;
/*
	HAL_GpioSet(E_HAL_GPIO_O_WIFI_EN);
	HAL_GpioSet(E_HAL_GPIO_O_GPS_PWR);
	HAL_GpioSet(E_HAL_GPIO_O_GPS_PWRKEY);
	HAL_DelayMs(400);
    HAL_GpioReset(E_HAL_GPIO_O_GPS_PWRKEY);
	HAL_DelayMs(600);
	HAL_GpioSet(E_HAL_GPIO_O_GPS_PWRKEY);
*/
	HAL_GpioSet(E_HAL_GPIO_O_WIFI_EN);
	HAL_GpioSet(E_HAL_GPIO_O_GPS_PWR);
	HAL_DelayMs(100);
	HAL_GpioSet(E_HAL_GPIO_O_GPS_PWRKEY);
	HAL_DelayMs(600);
    HAL_GpioReset(E_HAL_GPIO_O_GPS_PWRKEY);
    
	HAL_UartSetBaudRate(E_HAL_UART_EC20, E_HAL_UART_BAUD_115200, E_HAL_UART_WORD_8, E_HAL_UART_PARITY_NO);
    HAL_UartSetPackInterval(E_HAL_UART_EC20, 20);
    HAL_UartSetCallback(E_HAL_UART_EC20, appec20RcvInitDataProcess, NULL);

	EVT_Creat(appec20SendCmdEvt, &g_evtEc20CmdID); 
	
	u32Result = TMR_Creat(CMD_TIMEOUT, appec20ReSendCmdTimeOut, NULL, &g_tmrEc20CmdID);
    ASSERT(ERR_NONE == u32Result);

	u32Result = TMR_CreatRepeatTimer(HEART_TIME, appec20HeartTimeOut, NULL, &g_tmrHeartID);
    ASSERT(ERR_NONE == u32Result);

	u32Result = TMR_CreatRepeatTimer(CSQ_TIME, appec20CheckCsqTimeOut, NULL, &g_tmrCsqID);
    ASSERT(ERR_NONE == u32Result);

    LED_Play(eLED_ID_4G, m_eLED_PLAY_QUICK);
    LED_Play(eLED_ID_GPS, m_eLED_PLAY_ON);
    
	DBG(DBG_D, "EC20_Init OK!");
	return;
}


