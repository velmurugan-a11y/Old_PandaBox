/*
************************************************************************************************************************
*
*  文件: app_cfg.c
*  作者: 徐文杰
*  版本: V1.0.0
*  描述: Application，APP模块组
*
************************************************************************************************************************
*/
#include "app_cfg.h"
#include <stdio.h>
#include <stdlib.h>

T_BOX_PARAM g_stBoxParam;

E_SEND_MODE g_eSendMode = E_SEND_BT;


/*
************************************************************************************************************************
*  功能: 设置数据发送模式
*
*  描述: 无
*
*  参数: 无
*
*  返回: 无
*
************************************************************************************************************************
*/
void APPCFG_SetSendMode(E_SEND_MODE eSendMode)
{
	g_eSendMode = eSendMode;
	return;
}


/*
************************************************************************************************************************
*  功能: 获取数据发送模式
*
*  描述: 无
*
*  参数: 无
*
*  返回: 无
*
************************************************************************************************************************
*/
E_SEND_MODE APPCFG_GetSendMode(void)
{
	return g_eSendMode;
}

/*
************************************************************************************************************************
*  功能: 设置BOX配置参数
*
*  描述: 无
*
*  参数: 无
*
*  返回: 无
*
************************************************************************************************************************
*/
void APPCFG_SetBoxParam(T_BOX_PARAM *pstBoxParam)
{
	MUINT8 u8Index = 0;
	
	g_stBoxParam = *pstBoxParam;
	for(u8Index = 1; u8Index < 16; u8Index++)
	{
		if(g_stBoxParam.u8BtNameFlag == 0x5a)
		{
			if(g_stBoxParam.au8BtName[u8Index-1] == 0x0d && g_stBoxParam.au8BtName[u8Index] == 0x0a)
			{
				g_stBoxParam.au8BtName[u8Index-1] = 0;
			}
		}
		if(g_stBoxParam.u8WifiPwdFlag == 0x5a)
		{
			if(g_stBoxParam.au8WifiPwd[u8Index-1] == 0x0d && g_stBoxParam.au8WifiPwd[u8Index] == 0x0a)
			{
				g_stBoxParam.au8WifiPwd[u8Index-1] = 0;
			}
		}
		if(g_stBoxParam.u8WifiSsidFlag == 0x5a)
		{
			if(g_stBoxParam.au8WifiSsid[u8Index-1] == 0x0d && g_stBoxParam.au8WifiSsid[u8Index] == 0x0a)
			{
				g_stBoxParam.au8WifiSsid[u8Index-1] = 0;
			}
		}
		
	}
	HAL_FlashUserDataWrite(0, (MUINT8*)&g_stBoxParam, sizeof(T_BOX_PARAM));
	
	return;
}

/*
************************************************************************************************************************
*  功能: 获取BOX配置参数
*
*  描述: 无
*
*  参数: 无
*
*  返回: 无
*
************************************************************************************************************************
*/
T_BOX_PARAM APPCFG_GetBoxParam(void)
{

	MUINT8 u8Index = 0;
	
	for(u8Index = 1; u8Index < 16; u8Index++)
	{
		if(g_stBoxParam.u8BtNameFlag == 0x5a)
		{
			if(g_stBoxParam.au8BtName[u8Index-1] == 0x0d && g_stBoxParam.au8BtName[u8Index] == 0x0a)
			{
				g_stBoxParam.au8BtName[u8Index-1] = 0;
			}
		}
		if(g_stBoxParam.u8WifiPwdFlag == 0x5a)
		{
			if(g_stBoxParam.au8WifiPwd[u8Index-1] == 0x0d && g_stBoxParam.au8WifiPwd[u8Index] == 0x0a)
			{
				g_stBoxParam.au8WifiPwd[u8Index-1] = 0;
			}
		}
		if(g_stBoxParam.u8WifiSsidFlag == 0x5a)
		{
			if(g_stBoxParam.au8WifiSsid[u8Index-1] == 0x0d && g_stBoxParam.au8WifiSsid[u8Index] == 0x0a)
			{
				g_stBoxParam.au8WifiSsid[u8Index-1] = 0;
			}
		}
		
	}

	return g_stBoxParam;
}

void APPCFG_ReDefaultBoxParam(void)
{
	memset(&g_stBoxParam, 0, sizeof(T_BOX_PARAM));
	
    MEMCPY(g_stBoxParam.au8ServerIp, "139.9.203.9", 11);
    g_stBoxParam.au8ServerIp[11] = 0;

    MEMCPY(g_stBoxParam.au8ServerPort, "80", 2);
    g_stBoxParam.au8ServerPort[2] = 0;

    HAL_FlashUserDataWrite(0, (MUINT8*)&g_stBoxParam, sizeof(T_BOX_PARAM));
    
	return;
}

/*
************************************************************************************************************************
*  功能: 从FLASH中读取BOX配置参数
*
*  描述: 无
*
*  参数: 无
*
*  返回: 无
*
************************************************************************************************************************
*/
void APPCFG_Init(void)
{
	memset(&g_stBoxParam, 0, sizeof(g_stBoxParam));
	HAL_FlashUserDataRead(0, (MUINT8*)&g_stBoxParam, sizeof(g_stBoxParam));

	DBG(DBG_I, "\n\rWIFI SSID: %s\n\rWIFI PWD: %s\n\rBT NAME:%s\n\rSERVER IP:%s\n\rSERVER PORT:%s\n\r", 
	g_stBoxParam.au8WifiSsid, g_stBoxParam.au8WifiPwd, g_stBoxParam.au8BtName, g_stBoxParam.au8ServerIp,g_stBoxParam.au8ServerPort);
    return;
}


