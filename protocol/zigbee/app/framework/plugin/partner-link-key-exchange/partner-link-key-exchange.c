/***************************************************************************//**
 * @file
 * @brief This file handles the functionality to establish a link key
 * between two devices, neither of which are the TC.   This is called
 * a "Partner Link Key Request" and is done as follows:
 *   1) Device A sends Binding Request to Device B to bind the Key
 *      Establishment Cluster.
 *   2) If Device B has the capacity to support another link key, and allows
 *      the Partner Link Key Request it returns a Binding result of
 *      ZDO Success.
 *   3) Device A receives the binding response and sends an APS Command
 *      of Request Key, type Application Link Key, to the Trust Center.
 *******************************************************************************
 * # License
 * <b>Copyright 2018 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

#include "app/framework/include/af.h"
#include "app/util/zigbee-framework/zigbee-device-common.h" //emberBindRequest
#ifdef EZSP_HOST
  #include "app/util/zigbee-framework/zigbee-device-host.h" //emberNetworkAddressRequest
#endif
#include "partner-link-key-exchange.h"

#include "app/framework/util/af-main.h"
#include "stack/include/zigbee-security-manager.h"

sl_zigbee_event_t emberAfPluginPartnerLinkKeyExchangeTimeoutNetworkEvents[EMBER_SUPPORTED_NETWORKS];
void emberAfPluginPartnerLinkKeyExchangeTimeoutNetworkEventHandler(sl_zigbee_event_t * event);
typedef struct {
  bool active;
  EmberNodeId target;
  uint8_t targetEndpoint;
  uint8_t initiatorEndpoint;
  EmberAfPartnerLinkKeyExchangeCallback *callback;
} PartnerLinkKeyExchangeState;
static PartnerLinkKeyExchangeState states[EMBER_SUPPORTED_NETWORKS];

static void partnerLinkKeyExchangeComplete(bool success);
static EmberZdoStatus validateKeyRequest(EmberEUI64 partnerEui64);
static void addressDiscoveryCallback(const EmberAfServiceDiscoveryResult* result);
static EmberStatus continuePartnerLinkKeyExchangeCallback(EmberNodeId target,
                                                          EmberEUI64 source,
                                                          uint8_t endpoint,
                                                          uint8_t destinationEndpoint);

bool sli_zigbee_af_allow_partner_link_key = true;

//-----Internal callback

void sli_zigbee_af_partner_link_key_exchange_init_callback(uint8_t init_level)
{
  (void)init_level;

  sl_zigbee_network_event_init(emberAfPluginPartnerLinkKeyExchangeTimeoutNetworkEvents,
                               emberAfPluginPartnerLinkKeyExchangeTimeoutNetworkEventHandler);
}

//-----
EmberStatus emberAfInitiatePartnerLinkKeyExchangeCallback(EmberNodeId target,
                                                          uint8_t endpoint,
                                                          EmberAfPartnerLinkKeyExchangeCallback *callback)
{
  PartnerLinkKeyExchangeState *state = &states[emberGetCurrentNetwork()];
  EmberEUI64 source;
  EmberStatus status;
  uint8_t destinationEndpoint;

  if (state->active) {
    emberAfKeyEstablishmentClusterPrintln("%pPartner link key exchange in progress",
                                          "Error: ");
    return EMBER_INVALID_CALL;
  }

  destinationEndpoint = emberAfPrimaryEndpointForCurrentNetworkIndex();
  if (destinationEndpoint == 0xFF) {
    return EMBER_INVALID_CALL;
  }

  status = emberLookupEui64ByNodeId(target, source);
  if (status != EMBER_SUCCESS) {
    emberAfKeyEstablishmentClusterPrintln("Starting %p for node 0x%2x",
                                          "IEEE discovery",
                                          target);
    status = emberAfFindIeeeAddress(target, addressDiscoveryCallback);
  } else {
    status = continuePartnerLinkKeyExchangeCallback(target,
                                                    source,
                                                    endpoint,
                                                    destinationEndpoint);
  }

  if (status == EMBER_SUCCESS) {
    state->active = true;
    state->target = target;
    state->targetEndpoint = endpoint;
    state->initiatorEndpoint = destinationEndpoint;
    state->callback = callback;
    emberAfAddToCurrentAppTasks(EMBER_AF_WAITING_FOR_PARTNER_LINK_KEY_EXCHANGE);
    sl_zigbee_event_set_delay_ms(emberAfPluginPartnerLinkKeyExchangeTimeoutNetworkEvents,
                                 EMBER_AF_PLUGIN_PARTNER_LINK_KEY_EXCHANGE_TIMEOUT_MILLISECONDS);
  }
  return status;
}

static void addressDiscoveryCallback(const EmberAfServiceDiscoveryResult* result)
{
  if (emberAfHaveDiscoveryResponseStatus(result->status)
      && (result->zdoRequestClusterId == IEEE_ADDRESS_REQUEST
          || result->zdoRequestClusterId == NETWORK_ADDRESS_REQUEST)) {
    // We get here under two circumstances:
    // 1. At the start of Partner Link Key Exchange (PLKE), when the initiator
    //    needs to find the target's IEEE address;
    // 2. At the end of PLKE, when the target needs to find the initiator's
    //    node ID.
    // In both cases, we need to update the address table but only in the first
    // case we need to continue the PLKE process.
    uint8_t* eui64ptr = (uint8_t*)(result->responseData);
    emberAfKeyEstablishmentClusterPrintln("%p response: 0x%2X = %X%X%X%X%X%X%X%X",
                                          result->zdoRequestClusterId == IEEE_ADDRESS_REQUEST
                                          ? "IEEE discovery"
                                          : "NWK Address discovery",
                                          result->matchAddress,
                                          eui64ptr[7],
                                          eui64ptr[6],
                                          eui64ptr[5],
                                          eui64ptr[4],
                                          eui64ptr[3],
                                          eui64ptr[2],
                                          eui64ptr[1],
                                          eui64ptr[0]);
#ifdef EMBER_AF_HAS_END_DEVICE_NETWORK
    // On a router, we rely on the stack caching the NodeId -> EUI64 mapping
    // in a routing table. But on an end device, there is no routing table and
    // we need to cache it ourselves in the address table.
    if (emberAfAddAddressTableEntry(eui64ptr, result->matchAddress) == EMBER_NULL_ADDRESS_TABLE_INDEX) {
      emberAfKeyEstablishmentClusterPrintln("Cannot store IEEE address, table full.");
      partnerLinkKeyExchangeComplete(false);    // failure
    } else
#endif // EMBER_AF_HAS_END_DEVICE_NETWORK
    if (result->zdoRequestClusterId == IEEE_ADDRESS_REQUEST) {
      PartnerLinkKeyExchangeState *state = &states[emberGetCurrentNetwork()];
      if (continuePartnerLinkKeyExchangeCallback(result->matchAddress,
                                                 eui64ptr,
                                                 state->initiatorEndpoint,
                                                 state->targetEndpoint) != EMBER_SUCCESS) {
        partnerLinkKeyExchangeComplete(false);  // failure
      }
    }
  }
}

static EmberStatus continuePartnerLinkKeyExchangeCallback(EmberNodeId target,
                                                          EmberEUI64 source,
                                                          uint8_t endpoint,
                                                          uint8_t destinationEndpoint)
{
  EmberEUI64 destination;
  EmberStatus status;

  status = validateKeyRequest(source);
  if (status != EMBER_ZDP_SUCCESS) {
    emberAfKeyEstablishmentClusterPrintln("%p%p: 0x%x",
                                          "Error: ",
                                          "Cannot perform partner link key exchange",
                                          status);
    return status;
  }

  emberAfGetEui64(destination);
  status = emberBindRequest(target,
                            source,
                            endpoint,
                            ZCL_KEY_ESTABLISHMENT_CLUSTER_ID,
                            UNICAST_BINDING,
                            destination,
                            0, // multicast group identifier - ignored
                            destinationEndpoint,
                            EMBER_APS_OPTION_NONE);
  if (status != EMBER_SUCCESS) {
    emberAfKeyEstablishmentClusterPrintln("%p%p: 0x%x",
                                          "Error: ",
                                          "Failed to send bind request",
                                          status);
  }
  return status;
}

EmberZdoStatus emberAfPartnerLinkKeyExchangeRequestCallback(EmberEUI64 partner)
{
  EmberZdoStatus status = validateKeyRequest(partner);
  if (status != EMBER_ZDP_SUCCESS) {
    emberAfKeyEstablishmentClusterPrint("%pRejected parter link key request from ",
                                        "Error: ");
    emberAfKeyEstablishmentClusterDebugExec(emberAfPrintBigEndianEui64(partner));
    emberAfKeyEstablishmentClusterPrintln(": 0x%x", status);
    return status;
  }

  // Manually create an address table entry for the remote node so that the
  // stack will track its network address.  We can't send a network address
  // request here because we are not in the right stack context.
  if (emberAfAddAddressTableEntry(partner, EMBER_UNKNOWN_NODE_ID)
      == EMBER_NULL_ADDRESS_TABLE_INDEX) {
    emberAfKeyEstablishmentClusterPrint("WARN: Could not create address table entry for ");
    emberAfKeyEstablishmentClusterDebugExec(emberAfPrintBigEndianEui64(partner));
    emberAfKeyEstablishmentClusterPrintln("");
  }

  if (!sli_zigbee_af_allow_partner_link_key) {
    emberAfKeyEstablishmentClusterPrintln("Partner link key not allowed.");
    return EMBER_ZDP_NOT_PERMITTED;
  }
  return EMBER_ZDP_SUCCESS;
}

void emberAfPartnerLinkKeyExchangeResponseCallback(EmberNodeId sender,
                                                   EmberZdoStatus status)
{
  PartnerLinkKeyExchangeState *state = &states[emberGetCurrentNetwork()];
  if (state->active && state->target == sender) {
    EmberEUI64 partner;
    if (status != EMBER_ZDP_SUCCESS) {
      emberAfKeyEstablishmentClusterPrintln("%pNode 0x%2x rejected partner link key request: 0x%x",
                                            "Error: ",
                                            sender,
                                            status);
      partnerLinkKeyExchangeComplete(false); // failure
      return;
    }
    if (emberLookupEui64ByNodeId(sender, partner) != EMBER_SUCCESS) {
      emberAfKeyEstablishmentClusterPrintln("%pIEEE address of node 0x%2x is unknown",
                                            "Error: ",
                                            sender);
      partnerLinkKeyExchangeComplete(false); // failure
      return;
    }
    {
      EmberStatus requestLinkKeyStatus = emberRequestLinkKey(partner);
      if (requestLinkKeyStatus != EMBER_SUCCESS) {
        emberAfKeyEstablishmentClusterPrintln("%p%p: 0x%x",
                                              "Error: ",
                                              "Failed to request link key",
                                              requestLinkKeyStatus);
        partnerLinkKeyExchangeComplete(false); // failure
        return;
      }
    }
  }
}

void emberAfPluginPartnerLinkKeyExchangeZigbeeKeyEstablishmentCallback(EmberEUI64 partner,
                                                                       EmberKeyStatus status)
{
  PartnerLinkKeyExchangeState *state;
  EmberNodeId nodeId;

  (void) emberAfPushCallbackNetworkIndex();

  state = &states[emberGetCurrentNetwork()];

  // If we can't look up the node id using the long address, we need to send a
  // network address request.  This should only occur when we did not initiate
  // the partner link key exchange, so we don't need to worry about the node id
  // check below that fires the callback.  The stack will update the address
  // table entry that we created above when the response comes back.
  nodeId = emberLookupNodeIdByEui64(partner);
  if (nodeId == EMBER_NULL_NODE_ID) {
    emberAfFindNodeId(partner, addressDiscoveryCallback);
  }

  emberAfKeyEstablishmentClusterPrintln((status <= EMBER_TRUST_CENTER_LINK_KEY_ESTABLISHED
                                         ? "Key established: %d"
                                         : ((status >= EMBER_TC_RESPONDED_TO_KEY_REQUEST
                                             && status <= EMBER_TC_APP_KEY_SENT_TO_REQUESTER)
                                            ? "TC answered key request: %d"
                                            : "Failed to establish key: %d")),
                                        status);

  if (state->active && state->target == nodeId) {
    partnerLinkKeyExchangeComplete(status == EMBER_APP_LINK_KEY_ESTABLISHED);
  }

  (void) emberAfPopNetworkIndex();
}

void emberAfPluginPartnerLinkKeyExchangeTimeoutNetworkEventHandler(sl_zigbee_event_t * event)
{
  partnerLinkKeyExchangeComplete(false); // failure
}

static void partnerLinkKeyExchangeComplete(bool success)
{
  PartnerLinkKeyExchangeState *state = &states[emberGetCurrentNetwork()];

  state->active = false;
  sl_zigbee_event_set_inactive(emberAfPluginPartnerLinkKeyExchangeTimeoutNetworkEvents);
  emberAfRemoveFromCurrentAppTasks(EMBER_AF_WAITING_FOR_PARTNER_LINK_KEY_EXCHANGE);

  if (state->callback != NULL) {
    (*state->callback)(success);
  }
}

static EmberZdoStatus validateKeyRequest(EmberEUI64 partner)
{
  sl_zb_sec_man_context_t context;
  sl_zb_sec_man_aps_key_metadata_t key_data;
  sl_zb_sec_man_init_context(&context);

  context.core_key_type = SL_ZB_SEC_MAN_KEY_TYPE_TC_LINK;

  // Partner link key requests are not valid for the trust center because it
  // already has a link key with all nodes on the network.
  if (emberAfGetNodeId() == EMBER_TRUST_CENTER_NODE_ID) {
    return EMBER_ZDP_NOT_PERMITTED;
  }

  // We must have an authorized trust center link key before we are able to
  // process link key exchanges with other nodes.
  if (SL_STATUS_OK != sl_zb_sec_man_get_aps_key_info(&context, &key_data)
      || !(key_data.bitmask & EMBER_KEY_IS_AUTHORIZED)) {
    return EMBER_ZDP_NOT_AUTHORIZED;
  }

  // We need an existing entry or an empty entry in the key table to process a
  // partner link key exchange.
  if (!sl_zigbee_sec_man_link_key_slot_available(partner)) {
    return EMBER_ZDP_TABLE_FULL;
  }

  return EMBER_ZDP_SUCCESS;
}
