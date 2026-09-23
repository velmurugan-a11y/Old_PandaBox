int main(void)
{
	HAL_Init();
	SYS_Init();
	DRV_Init();
	APP_Init();
	CLI_Init();

	while(1)
	{
		HAL_FeedWatchDog();
		HAL_DoEvent();
		EVT_DoEvent();
		MQ_ProcessMsg();
		TMR_ProcessTimeout();
	}
}

/*
************************************************************************************************************************
*  功能: HAL层初始化
*
*  描述: HAL层初始化，初始化所有MCU相关硬件接口和模块
*
*  参数: 无
*
*  返回: 无
*
************************************************************************************************************************
*/
void HAL_Init(void)
{
    MCU_Init();    
    HWEVT_Init();
    GPIO_Initialize();
    UART_Init();
    RTC_Init();
    IT_Init();
    //IIC_Init();    
    SPI_Initialize();
    ADC_Initialize();
    FLASH_Init();
    WDG_Init();
    ASSERT_Init(halGetUartID(E_HAL_UART_PRINT));
    return;
}

/*
************************************************************************************************************************
*  功能: 系统公共模块初始化
*
*  描述: 系统公共模块
*
*  参数: 无
*
*  返回: 无
*
************************************************************************************************************************
*/
void SYS_Init(void)
{
    MUINT32 u32Result;
    MUINT32 u32Index;
    
    // 初始化系统复位回调函数
    for (u32Index = 0; u32Index < SYS_REBOOT_CB_NUM; u32Index++)
    {
        g_pfnRebootCB[u32Index] = NULL;
    }

    // 获取复位原因
    //g_eRebootReason = HAL_GetRebootReason();

    // 初始化各模块
    DBG_Init();
    //VER_Init();
    TMR_Init();
    EVT_Init();
	MQ_Init();
    //ISR_Init();
	
    // 创建系统消息队列
    memset(&g_stMqMgr, 0, sizeof(T_SYS_MQ_MGR));
    u32Result = MQ_Creat(sysMqCallback, &g_stMqMgr.mq);
    ASSERT(ERR_NONE == u32Result);

    //DBG_PrintLine("System reboot, reason: %s", g_apszSysRebootReason[g_eRebootReason]);

    return;
}

/*
************************************************************************************************************************
*  功能: DRV模块组的初始化
*
*  描述: 所有MCU外接的硬件模块驱动的初始化统一入口
*
*  参数: 无
*
*  返回: 无
*
************************************************************************************************************************
*/
void DRV_Init(void)
{
	MUINT32 u32GdFlashId = 0;
	
	LED_Init();
	//PDDETECT_Init();
	gd25q256df_init();

	u32GdFlashId = gd25q256df_read_id();

	DBG(DBG_D, "u32GdFlashId = %X", u32GdFlashId);
	
    return;
}


/*
************************************************************************************************************************
*  功能: APP模块组的初始化
*
*  描述: APP入口
*
*  参数: 无
*
*  返回: 无
*
************************************************************************************************************************
*/
void APP_Init(void)
{
	MUINT32 u32Result = ERR_NONE;

	SYS_SetSysMsgCallback(appProcessSysMsg);
	
	DBG(DBG_I, "---APP Init Start!---");

	LED_Play(eLED_ID_4G, m_eLED_PLAY_QUICK);
	LED_Play(eLED_ID_BT, m_eLED_PLAY_QUICK);
	LED_Play(eLED_ID_WIFI, m_eLED_PLAY_QUICK);
    LED_Play(eLED_ID_GPS, m_eLED_PLAY_QUICK);
    
	u32Result = TMR_Creat(3000, appInitTimeOut, NULL, &g_tmrInitID);
    ASSERT(ERR_NONE == u32Result);
	TMR_Start(g_tmrInitID);
	
    return;
}


/*
************************************************************************************************************************
*  功能: APP模块组的初始化
*
*  描述: APP入口
*
*  参数: 无
*
*  返回: 无
*
************************************************************************************************************************
*/
void APP_Init(void)
{
	MUINT32 u32Result = ERR_NONE;

	SYS_SetSysMsgCallback(appProcessSysMsg);
	
	DBG(DBG_I, "---APP Init Start!---");

	LED_Play(eLED_ID_4G, m_eLED_PLAY_QUICK);
	LED_Play(eLED_ID_BT, m_eLED_PLAY_QUICK);
	LED_Play(eLED_ID_WIFI, m_eLED_PLAY_QUICK);
    LED_Play(eLED_ID_GPS, m_eLED_PLAY_QUICK);
    
	u32Result = TMR_Creat(3000, appInitTimeOut, NULL, &g_tmrInitID);
    ASSERT(ERR_NONE == u32Result);
	TMR_Start(g_tmrInitID);
	
    return;
}

