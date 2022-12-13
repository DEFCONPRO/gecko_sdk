/***************************************************************************//**
 * @file
 * @brief HTTP Client Configuration - Configuration Template File
 *******************************************************************************
 * # License
 * <b>Copyright 2018 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc.  Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement.  This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

/********************************************************************************************************
 ********************************************************************************************************
 *                                               MODULE
 ********************************************************************************************************
 *******************************************************************************************************/

#ifndef  _HTTP_CLIENT_CFG_H_
#define  _HTTP_CLIENT_CFG_H_

/********************************************************************************************************
 ********************************************************************************************************
 *                                             TASK OPTIONS
 ********************************************************************************************************
 *******************************************************************************************************/

#define  HTTPc_CFG_MODE_ASYNC_TASK_EN                       DEF_ENABLED

#define  HTTPc_CFG_MODE_BLOCK_EN                            DEF_ENABLED

/********************************************************************************************************
 ********************************************************************************************************
 *                                         HTTP FEATURE OPTIONS
 ********************************************************************************************************
 *******************************************************************************************************/

#define  HTTPc_CFG_PERSISTENT_EN                            DEF_ENABLED

#define  HTTPc_CFG_CHUNK_TX_EN                              DEF_ENABLED

#define  HTTPc_CFG_QUERY_STR_EN                             DEF_ENABLED

#define  HTTPc_CFG_HDR_RX_EN                                DEF_ENABLED

#define  HTTPc_CFG_HDR_TX_EN                                DEF_ENABLED

#define  HTTPc_CFG_FORM_EN                                  DEF_ENABLED

#define  HTTPc_CFG_USER_DATA_EN                             DEF_ENABLED

#define  HTTPc_CFG_WEBSOCKET_EN                             DEF_ENABLED

/********************************************************************************************************
 ********************************************************************************************************
 *                                             MODULE END
 ********************************************************************************************************
 *******************************************************************************************************/

#endif // End of http_client_cfg.h module include.