#include "includes.h"
#include <string.h>
#include <stdlib.h>
//extern volatile RingBuffer_t Tx_buffer;
extern SemaphoreHandle_t xSemaphore_4G;

uint8_t gprs_buf[128] = {0};   //GSM接受缓冲区，全局变量

static void Gsm_RecvInit(void)
{
    memset(gprs_buf,0,sizeof(gprs_buf));
    USART2_ClearBuf();
}

static void Gsm_RecvCmd(void)
{
    USART2_Rx(gprs_buf,sizeof(gprs_buf));
}

//=============================================================
//函 数 名: Gsm_SendAndWait
//输入参数: cmd      :发送的命令字符串
//			strwait  :需要等待的字符串
//			num_sema :总共需要同步的信号量的个数，有的数据回应时，数据包会被分成几个部分
//			trynum   :重传的次数
//			timeout  :超时时间，信号量等待的时间
//返 回 值: 0:成功，1：失败
//功能描述: 向SIM7600 发送AT命令，并等待数据回包
//=============================================================
static uint8_t Gsm_SendAndWait(uint8_t *cmd,uint8_t *strwait,uint8_t num_sema,uint8_t trynum,uint32_t timeout)
{
    char *p;
    BaseType_t seam_ret = pdFAIL;
	for(int i = 0 ; i < trynum ; i++)
	{
		//尝试发送
		Gsm_RecvInit();        //清除缓冲区
		while(1)
		{
			if(xSemaphoreTake( xSemaphore_4G,0 ) != pdPASS)     //清除信号量
				break;
		}
		SIM7600_SendStr(cmd);
        for(int i = 0 ; i < num_sema ; i++)
        {
            seam_ret = xSemaphoreTake( xSemaphore_4G,timeout);
            if(seam_ret != pdPASS)
                break;
            Gsm_RecvCmd();
            p = strstr((char*)gprs_buf,(char*)"ERROR");
            if(p)
                break;
            else
            {
                p = strstr((char*)gprs_buf,(char*)strwait);
                if(p)
                   return 0;
            }
        }
	}
	return 1; 
}

uint8_t get_csq(uint8_t *val)
{
    char *p;
    uint8_t value = 0;
    *val = value;
    if(Gsm_SendAndWait((uint8_t *)"AT+CSQ\r\n",(uint8_t *)"+CSQ: ",1,RETRY_NUM,1000))
        return 1;
    p = strstr((char*)gprs_buf,(char*)"+CSQ:");
    if(p)
    {
        value = atoi(p+6);
    }
    *val = value;
    return 0;
}

static uint8_t Gsm_AT_CREG(uint8_t* stat)
{
	char *pst;

	if(Gsm_SendAndWait((uint8_t *)"AT+CREG?\r\n",(uint8_t *)"+CREG: ",1,RETRY_NUM,1000))
        return 1;
    pst  = strstr((char*)gprs_buf,"+CREG: "); //+CREG: 0,1
    *stat = atoi(pst+9);	
	return 0 ;
}


static uint8_t Gsm_AT_CPSI(void)
{
	char *pst , *psec;
    char *p;

    char buf[30] = {0};
	if(Gsm_SendAndWait((uint8_t *)"AT+CPSI?\r\n",(uint8_t *)"+CPSI: ",1,RETRY_NUM,2000))
        return 1;

    pst  = strstr((char*)gprs_buf,"+CPSI: ");
    psec  = strstr((char*)gprs_buf,",");
    memcpy(buf,pst,psec - pst);	
    p = strstr(buf,"NO SERVICE");
    if(p)
        return 1;
    else	
        return 0 ;
}

static uint8_t Gsm_AT_CGREG(uint8_t* stat)
{
	char *pst;

	if(Gsm_SendAndWait((uint8_t *)"AT+CGREG?\r\n",(uint8_t *)"+CGREG: ",1,RETRY_NUM,1000))
        return 1;
    pst  = strstr((char*)gprs_buf,"+CGREG: "); //+CGREG: 0,1
    *stat = atoi(pst+10);	
	return 0 ;
}

//AT+CIPRXGET=1
static uint8_t Gsm_AT_CIPRXGET(void)
{
	if(Gsm_SendAndWait((uint8_t *)"AT+CIPRXGET=1\r\n",(uint8_t *)"OK",1,RETRY_NUM,1000))
        return 1;        
	else
        return 0 ;
}


//AT+NETOPEN
static uint8_t Gsm_AT_NETOPEN(void)
{
	if(Gsm_SendAndWait((uint8_t *)"AT+NETOPEN\r\n",(uint8_t *)"+NETOPEN: 0",2,RETRY_NUM,2000))
        return 1;
    else
        return 0;
}
//AT+CIPOPEN=0,"TCP","218.244.156.4",6886
static uint8_t Gsm_AT_CIPOPEN(uint8_t *ip ,uint32_t port,uint8_t channel)
{
    uint8_t inf[50] = {0};
    sprintf((char*)inf,"AT+CIPOPEN=%d,\"TCP\",\"%s\",%d\r\n",channel,ip,port);	
	if(Gsm_SendAndWait(inf,(uint8_t *)"OK",2,RETRY_NUM,3000))
        return 1;
    else
        return 0;
}

////AT+CGSOCKCONT=1,"IP","CMNET"
////AT+CSOCKSETPN=1
static uint8_t Gsm_Stask_Spoint(uint8_t *point)
{
	uint8_t inf[50];
	sprintf((char*)inf,"AT+CGSOCKCONT=1,\"IP\",\"%s\"\r\n",point);
	if(!Gsm_SendAndWait(inf,(uint8_t *)"OK\r\n",1,RETRY_NUM,1000))
        return  Gsm_SendAndWait((uint8_t *)"AT+CSOCKSETPN=1\r\n",(uint8_t *)"OK\r\n",1,RETRY_NUM,1000);
    else
        return 1;
}

static uint8_t Gsm_set_tcpip_app_mode(uint8_t type)
{//TCPIP应用模式(0  非透传模式    1透传模式)
	if(DEFAULT_TANS_MODE==type)
	{
		return	Gsm_SendAndWait((uint8_t *)"AT+CIPMODE=0\r\n",(uint8_t *)"OK\r\n",1,RETRY_NUM,1000);

	}
	return	Gsm_SendAndWait((uint8_t *)"AT+CIPMODE=1\r\n",(uint8_t *)"OK\r\n",1,RETRY_NUM,1000);
}

//关闭TCP/UDP连接  AT+CIPCLOSE
uint8_t Gsm_shutdowm_tcp_udp()
{
    if(Gsm_SendAndWait((uint8_t *)"AT+CIPCLOSE=DEFAULT_LINK_CHANNEL\r\n",(uint8_t *)"+CIPCLOSE:\r\n",2,RETRY_NUM,5000))
    {
        
    }
       
}

//关闭SOCKET  AT+NETCLOSE
uint8_t Gsm_shutdowm_socket()
{
    return Gsm_SendAndWait((uint8_t *)"AT+NETCLOSE\r\n",(uint8_t *)"+NETCLOSE: 0\r\n",3,RETRY_NUM,5000);
}

///***********************************************************************************
//*连接服务器
//***********************************************************************************/
uint8_t Gsm_Connect_Server(uint8_t *ip ,uint32_t port)
{
 	uint8_t csq;
	uint8_t stat;

	if(Gsm_SendAndWait((uint8_t *)"AT\r\n",(uint8_t *)"OK\r\n",1,1,1000))
	{
		return	CONNECT_ERR_AT; 
	}

    if(Gsm_SendAndWait((uint8_t *)"ATE0\r\n",(uint8_t *)"OK\r\n",1,1,1000))
	{
		return	CONNECT_ERR_ATE0; 
	}
    

    //Gsm_shutdowm_tcp_udp();
    
    Gsm_shutdowm_socket();

    

 	get_csq(&csq);

    if(Gsm_set_tcpip_app_mode(0))
    {
        return CONNECT_ERR_CIPMODE;
    }	
				
	
    if(Gsm_Stask_Spoint((uint8_t *)"CMNET"))
	{
	    return CONNECT_ERR_CSTT;
	} 
    
	if(Gsm_AT_CREG(&stat))
	{
	    return CONNECT_ERR_CREG;	
	}

 	
	if(Gsm_AT_CPSI())
	{
	    return CONNECT_ERR_CPSI;	
	}
	
	if(Gsm_AT_CGREG(&stat))
	{
	    return CONNECT_ERR_CGREG;	
	}
    

	if(Gsm_AT_CIPRXGET())  //手动接收字节
	{
	    return CONNECT_ERR_CIPRXGET;
	}


    if(Gsm_AT_NETOPEN())
	{
	    return CONNECT_ERR_NETOPEN;
	}		


    if(Gsm_AT_CIPOPEN(ip,port,DEFAULT_LINK_CHANNEL))
    {
            return CONNECT_ERR_CIPOPEN;
    }
	
	return CONNECT_ERR_NONE;
}

//uint8_t Gsm_SendAndWait2(uint8_t *cmd,uint8_t *strwait,uint8_t *strwait2,uint8_t num_sema,uint8_t trynum,uint32_t timeout)
//{
//    char *p;
//    BaseType_t seam_ret = pdFAIL;
//	for(int i = 0 ; i < trynum ; i++)
//	{
//		//尝试发送
//		Gsm_RecvInit();        //清除缓冲区
//		while(1)
//		{
//			if(xSemaphoreTake( xSemaphore_4G,0 ) != pdPASS)     //清除信号量
//				break;
//		}
//		SIM7600_SendStr(cmd);
//        for(int i = 0 ; i < num_sema ; i++)
//        {
//            seam_ret = xSemaphoreTake( xSemaphore_4G,timeout);
//            if(seam_ret != pdPASS)
//                return 1;
//        }
//		Gsm_RecvCmd();
//		p = strstr((char*)gprs_buf,(char*)strwait);
//		if(p)
//		   return 0;
//        else
//        {
//            p = strstr((char*)gprs_buf,(char*)strwait2);
//            if(p)
//                return 0;
//        }
//	}
//	return 1; 
//}
////
////uint8_t Gsm_wait(uint8_t *strwait,uint8_t trynum,uint8_t timeout)
////{
////    uint8_t   i;
////	char *p;	
////    
////    const portTickType xDelay = (50*timeout) / portTICK_RATE_MS;
////	xSemaphoreTake( xSemaphore_4G,portMAX_DELAY );
////    
////	for(i=0;i<trynum;i++)
////	{
////		vTaskDelay(xDelay);
////        Gsm_RecvCmd();
////		p = strstr((char*)gprs_buf,(char*)strwait);
////		if(p)
////		{
////		   xSemaphoreGive(xSemaphore_4G);
////		   return 0;
////		}
////	}
////	xSemaphoreGive(xSemaphore_4G);
////	return 1; 
////}
////
//
////
////
////void Gsm_csq(uint8_t* csq)
////{
////	uint8_t   rst;
////	char *pst;
////	uint8_t   temp;
////
////	rst =Gsm_SendAndWait((uint8_t *)"AT+CSQ\r\n",(uint8_t *)"+CSQ: ",RETRY_NUM,1);
////	if(!rst)
////	{
////		pst  = strstr((char*)gprs_buf,"+CSQ:");
////		temp = atoi(pst+6);	
////		*csq = temp;
////	}
////}
////
////关闭TCP/UDP连接  AT+CIPCLOSE
//uint8_t Gsm_shutdowm_tcp_udp()
//{
//    return Gsm_SendAndWait((uint8_t *)"AT+CIPCLOSE=DEFAULT_LINK_CHANNEL\r\n",(uint8_t *)"OK\r\n",2,RETRY_NUM,2000);
//}
//
////关闭SOCKET  AT+NETCLOSE
//uint8_t Gsm_shutdowm_socket()
//{
//    return Gsm_SendAndWait((uint8_t *)"AT+NETCLOSE\r\n",(uint8_t *)"OK\r\n",2,RETRY_NUM,2000);
//}
//
//static uint8_t Gsm_set_tcpip_app_mode(uint8_t type)
//{//TCPIP应用模式(0  非透传模式    1透传模式)
//	if(DEFAULT_TANS_MODE==type)
//	{
//		return	Gsm_SendAndWait((uint8_t *)"AT+CIPMODE=0\r\n",(uint8_t *)"OK\r\n",1,RETRY_NUM,1000);
//
//	}
//	return	Gsm_SendAndWait((uint8_t *)"AT+CIPMODE=1\r\n",(uint8_t *)"OK\r\n",1,RETRY_NUM,1000);
//}
//
//
//////AT+CGSOCKCONT=1,"IP","CMNET"
//////AT+CSOCKSETPN=1
//static uint8_t Gsm_Stask_Spoint(uint8_t *point)
//{
//	uint8_t inf[50];
//	sprintf((char*)inf,"AT+CGSOCKCONT=1,\"IP\",\"%s\"\r\n",point);
//	if(!Gsm_SendAndWait(inf,(uint8_t *)"OK\r\n",1,RETRY_NUM,1000))
//        return  Gsm_SendAndWait((uint8_t *)"AT+CSOCKSETPN=1\r\n",(uint8_t *)"OK\r\n",1,RETRY_NUM,1000);
//    else
//        return 1;
//
//}
//
//
//static uint8_t Gsm_Connect_Tcp_or_UdpPort(uint8_t *ip ,uint32_t port,uint8_t channel)
//{
//	uint8_t inf[50];
//
//   	
//    if(Gsm_SendAndWait((uint8_t *)"AT+NETOPEN\r\n",(uint8_t *)"OK",(uint8_t *)"Network is already opened" ,RETRY_NUM,4))
//        return 1;
//    
//    sprintf((char*)inf,"AT+CIPOPEN=%d,\"TCP\",\"%s\",\"%d\"\r\n",channel,ip,port);		
//		
//	return Gsm_SendAndWait(inf,(uint8_t *)"OK",RETRY_NUM,20);	
//}



//static uint8_t Gsm_AT_CREG(uint8_t* stat)
//{
//	uint8_t   rst;
//	char *pst;
//
//	rst =Gsm_SendAndWait((uint8_t *)"AT+CREG?\r\n",(uint8_t *)"+CREG: ",RETRY_NUM,1);
//	if(!rst)
//	{
//		pst  = strstr((char*)gprs_buf,"+CREG: "); //+CREG: 0,1
//		*stat = atoi(pst+9);	
//	}
//	return rst ;
//}
//
//
//
//static uint8_t Gsm_AT_CPSI(uint8_t* stat)
//{
//	uint8_t   rst = 1;
//	char *pst , *psec;
//    char *p;
//
//    char buf[30] = {0};
//	rst = Gsm_SendAndWait((uint8_t *)"AT+CPSI?\r\n",(uint8_t *)"OK",RETRY_NUM,2);
//
//	if(!rst)
//	{
//		pst  = strstr((char*)gprs_buf,"+CPSI: ");
//        psec  = strstr((char*)gprs_buf,",");
//		memcpy(buf,pst,psec - pst);	
//        p = strstr(buf,"NO SERVICE")
//        if(p)
//            *stat = 1;
//        else
//            *stat = 0;
//	}
//	return rst ;
//}
//
//
/**************************************************************
 *模块开机接口初始化
 *************************************************************/
void Gsm_TurnON(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    /* GPIO Ports Clock Enable */
    __HAL_RCC_GPIOD_CLK_ENABLE();

    /*Configure GPIO pin Output Level */
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_1, GPIO_PIN_RESET);

    /*Configure GPIO pin : PB1 */
    GPIO_InitStruct.Pin = GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
    //HAL_GPIO_WritePin(GPIOD, GPIO_PIN_1, GPIO_PIN_SET);
    vTaskDelay(pdMS_TO_TICKS(100));
    while(1)
    {
        if(Gsm_SendAndWait((uint8_t *)"AT\r\n",(uint8_t *)"OK",1,2,1000))//如果之前关机，则现在开机
        {
            HAL_GPIO_WritePin(GPIOD, GPIO_PIN_1, GPIO_PIN_RESET);
            vTaskDelay(pdMS_TO_TICKS(600));
            HAL_GPIO_WritePin(GPIOD, GPIO_PIN_1, GPIO_PIN_SET);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        else
        {
            break;
        }
            
    }
}
//

//
///**************************************************************
// *发送数据
// *************************************************************/
//uint8_t Gsm_Send_data(uint8_t *s, uint32_t size)
//{ 
//	uint8_t inf[50];
//	uint8_t mux = GPRS_CIPMUX_TYPE;
//	uint8_t channel = DEFAULT_LINK_CHANNEL;
//	if(1==mux)
//	{
//		sprintf((char*)inf,"AT+CIPSEND=%d,%d\r\n",channel,size);	
//	}
//	else
//	{
//		sprintf((char*)inf,"AT+CIPSEND=%d\r\n",size);	
//	}	
//
//	
//	if(Gsm_SendAndWait(inf,(uint8_t *)">",RETRY_NUM,1))
//	{
//		return CONNECT_ERR_CIPSEND;	
//	}
//
//	SIM7600_SendData(s,size);	
//	
//	
//	Gsm_wait((uint8_t *)"SEND OK",1,4);
//	
//	return CONNECT_ERR_NONE;
//}	
//
//uint16_t Gsm_Recv_data(uint8_t* buf, uint16_t size)
//{
//    //OS_ERR      err;
//    uint8_t rst;
//    uint16_t len=0;
//    uint16_t cnlen = 0;
//    uint16_t offset = 0;
//    char *pst=NULL;
//    uint8_t inf[50];
//    uint8_t mode = 2;
//	uint8_t mux = GPRS_CIPMUX_TYPE;
//	uint8_t channel = DEFAULT_LINK_CHANNEL;
//	if(1==mux)
//		sprintf((char*)inf,"AT+CIPRXGET=%d,%d,%d\r\n",mode,channel,size);	
//	else
//		sprintf((char*)inf,"AT+CIPRXGET=%d,%d\r\n",mode,size);	   
//    do
//    {
//        rst = Gsm_SendAndWait(inf,(uint8_t *)"+CIPRXGET: 2",RETRY_NUM,1);
//        if(!rst)
//	    {
//            pst = strstr((char*)gprs_buf,"+CIPRXGET: 2");
//            pst = pst+15;
//            len = atoi(pst);
//            cnlen = atoi(pst+3);	        
//            if(len!=0)
//            {            
//                //pst = strstr(pst,"0x0A"); 
//                pst = strchr(pst,'\n');
//                memcpy(buf+offset,pst+1,len);                  
//                offset += len;
//            }    
//            vTaskDelay(pdMS_TO_TICKS(50));
//        }
//	}while((cnlen != 0)&&(rst == 1));
//	return offset;
//	
//}
//uint8_t Gsm_CloseConnect()
//{
//    Gsm_shutdowm_tcp_udp();
//    Gsm_shutdowm_socket();
//}