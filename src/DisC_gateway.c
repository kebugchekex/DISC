#ifdef _WIN32
#include <windows.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

#include <jansson.h>

#include "DisC_types.h"
#include "DisC_util.h"
#include "DisC_errors.h"
#include "DisC_object.h"
#include "DisC_rest.h"
#include "DisC_gateway.h"




static short internal_CheckSendHeartbeat(DisC_session_t *session);

static char *internal_GetHTTPResponseCode(DisC_session_t *session, char *str);

static char *internal_GenerateUpgradeHeaders(DisC_session_t *session, char *b64Key, char *URI, char *host);

static unsigned char *internal_ReadData(DisC_session_t *session, unsigned long *dataLen, DisC_BOOL_t isHandshaking);

static short internal_WriteData(DisC_session_t *session, unsigned char *data, unsigned long dataLen);

//=======================================================================================================


static short internal_CheckSendHeartbeat(DisC_session_t *session)//TODO completely redo the networking system so long blocking operations don't interrupt heartbeats
{
  //TODO run this function from the check and manage function
  #ifdef _WIN32

  #else
  struct timespec *tick;
  tick = (struct timespec *)malloc(sizeof(tick));
  clock_gettime(CLOCK_MONOTONIC, &tick);
  #endif
  //check last tick heartbeat was sent at
  //if tick is after the tick wait, send a heartbeat, then check for opcode 11
  //otherwise, don' do anything

  //TODO make after doing the gateway identify
  /*
  internal_WriteData(session, request, strlen(request));
  returnData = internal_ReadData(session, &returnDataLen, DISC_TRUE);
  while(returnData == NULL && (errno == EAGAIN || errno == EWOULDBLOCK))
  {
    returnData = internal_ReadData(session, &returnDataLen, DISC_TRUE);
    sleep(1);
  }
  */

  return DISC_ERROR_NONE;
}

static char *internal_GetHTTPResponseCode(DisC_session_t *session, char *str)
{
  //make a temp string to verify the send
  //temporary fix for chunked response still sending data that code later mistakes for the end of headers
  if(*str == '0')
  {
    if(strstr(str, "\r\n\r\n") == &str[1])
    {
      str[1] = "eror";
    }
  }
  char *tempReturn = NULL;
  DisC_asprintf(&tempReturn, "%s", strstr(str, "HTTP/1.1 "));
  char *httpStatus = strtok(tempReturn, "\r\n");
  char *code = httpStatus + strlen("HTTP/1.1 ");
  strtok(code, " ");
  //printf("str: %s\ncode: %s\nand: %s\ncode: %s\n", str, httpStatus, tempReturn, code);
  //finish getting status

  char *returnStr = DisC_strmkdup(code);
  free(tempReturn);

  return returnStr;
}

static char *internal_GenerateUpgradeHeaders(DisC_session_t *session, char *b64Key, char *URI, char *host)
{
  char *finalString = NULL;
  char *base = "GET %s?v=6&encoding=json HTTP/1.1\r\nHost: %s\r\nConnection: keep-alive\r\nAuthorization: %s%s\r\nUser-Agent: (so sorry for not obeying rate limiting. Nearly at the point that I can add it)DisC v%d.%d.%d: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-webSocket-Key: %s\r\nSec-WebSocket-Protocol: chat, superchat\r\nSec-WebSocket-Version: 13\r\n\r\n";
  DisC_asprintf(&finalString, base, URI, host, session->clientType ? "Bot " : "", session->token, DISC_VER_MAJOR, DISC_VER_MINOR, DISC_VER_REVISION, session->internalName, b64Key);

  //char *base = "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-webSocket-Key: %s\r\nSec-WebSocket-Protocol: chat, superchat\r\nSec-WebSocket-Version: 13\r\n\r\n";
  //DisC_asprintf(&finalString, base, URI, host, b64Key);


  return finalString;
}


static unsigned char *internal_ReadData(DisC_session_t *session, unsigned long *dataLen, DisC_BOOL_t isHandshaking)
{
  char *buffer = NULL;
  unsigned long byteCount = 0;
  unsigned long byteChunkMax = 1024;
  unsigned long contentLength = 0;
  unsigned long status;
  int foundHTTPEnd = 0;

  buffer = malloc(byteChunkMax);
  if(buffer == NULL)
  {
    exit(1);
  }
  status = byteChunkMax;
  do {
    status = (unsigned long)BIO_read(session->DONOTSET_gateway_bio, buffer + byteCount, status);
    if(status == -1)
    {
      return NULL;
    }

    byteCount += status;

    buffer = realloc(buffer, byteCount + byteChunkMax);
    //printf("%d\n", status);


    //printf("buffer: %s\nstatus: %lu", buffer, status);
    if(strstr(buffer, "\r\n\r\n") != NULL && isHandshaking)
    {
      foundHTTPEnd = 1;
    }
    else if(strchr(buffer, 0x00))
    {
      foundHTTPEnd = 1;//reusing variable
    }



  } while(status > 0 && !foundHTTPEnd);




  buffer = realloc(buffer, byteCount);

  *dataLen = byteCount;
  return buffer;
}

static short internal_WriteData(DisC_session_t *session, unsigned char *data, unsigned long dataLen)
{
  if(BIO_write(session->DONOTSET_gateway_bio, data, dataLen) <= 0)
  {
    DisC_Log(session->logLevel, session->logFileLocation, DISC_SEVERITY_NOTIFY, "Couldnt send to gateway... At the moment, this means further library dysfunction because I dont want to risk being banned from the api if I pull something stupid on accident");
    DisC_AddError(session, DISC_ERROR_CONNECTION);
    return DISC_ERROR_CONNECTION;
  }

  DisC_AddError(session, DISC_ERROR_NONE);
  return DISC_ERROR_NONE;
}


short DisC_gateway_InitSession(DisC_session_t *session, DisC_callbacks_t *callbacks)
{
  DisC_Log(session->logLevel, session->logFileLocation, DISC_SEVERITY_NOTIFY, "Starting session %s gateway", session->internalName);
  short returnval;
  char *url = NULL;
  char *code = NULL;
  char *readData = NULL;
  unsigned long readDataLen = 0;

  DisC_asprintf(&url, session->clientType ? "%s/gateway/bot" : "%s/gateway", DISC_REST_APIBASE);
  DisC_REST_DiscordHTTPRequest(session, &readData, &readDataLen, &code, "GET", NULL, url, NULL, 0);


  char *replace = strstr(readData, "}");
  replace++;
  *replace = '\0';

  json_t *retRoot = NULL;

  //============================================

  if(strcmp(code, "200") == 0)
  {
    retRoot = json_loads(readData, 0, NULL);

    if(retRoot != NULL)
    {
      //make a nonblocking socket with ssl stuff
      unsigned long retries = 0;
      int result;
      char *wsurl = json_string_value(json_object_get(retRoot, "url"));
      wsurl += strlen("wss://");
      char *finalWSURL = NULL;
      DisC_asprintf(&finalWSURL, "%s:https", wsurl);

      //printf()
      //===========================================================


      session->DONOTSET_gateway_ctx = SSL_CTX_new(TLSv1_2_client_method());


      session->DONOTSET_gateway_bio = BIO_new_ssl_connect(session->DONOTSET_gateway_ctx);
      BIO_set_conn_hostname(session->DONOTSET_gateway_bio, finalWSURL);
      BIO_set_nbio(session->DONOTSET_gateway_bio, 1);
      result = BIO_do_connect(session->DONOTSET_gateway_bio);
      if(result <= 0)
      {
        while(BIO_should_retry(session->DONOTSET_gateway_bio) && retries != DISC_MAX_RECONNECTS)
        {
          DisC_Log(session->logLevel, session->logFileLocation, DISC_SEVERITY_ERROR, "Waiting for socket");
          result = BIO_do_connect(session->DONOTSET_gateway_bio);
          if(result > 0)
            break;


          sleep(1);//set to one second because this is the only one that actually works for some reason. BIO non blocking is really weird.
          retries++;//TODO wait for the socket the right way instead of sleeping. This is going to produce wildly different results for everything
        }
      }
      else
      {
        DisC_Log(session->logLevel, session->logFileLocation, DISC_SEVERITY_NOTIFY, "Connected to gateway first try");
        DisC_AddError(session, DISC_ERROR_NONE);
        returnval = DISC_ERROR_NONE;
      }

      if(result <= 0)
      {
        DisC_Log(session->logLevel, session->logFileLocation, DISC_SEVERITY_NOTIFY, "Could not connect to gateway after retrying max times or there was a serious error");
        DisC_AddError(session, DISC_ERROR_CONNECTION);
        returnval = DISC_ERROR_CONNECTION;
      }
      else
      {
        DisC_Log(session->logLevel, session->logFileLocation, DISC_SEVERITY_NOTIFY, "Handshaking with gateway");
        //do everything to make a websocket tcp Connection
        unsigned char *returnData = NULL;
        unsigned long returnDataLen = 0;
        char *jsonResponse = NULL;
        char *request = internal_GenerateUpgradeHeaders(session, "x3JJHMbDL1EzLkh9GBhXDw==", "/", wsurl);//TODO not use the wikipedia base64 haha
        internal_WriteData(session, request, strlen(request));
        returnData = internal_ReadData(session, &returnDataLen, DISC_TRUE);
        while(returnData == NULL && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
          returnData = internal_ReadData(session, &returnDataLen, DISC_TRUE);
          sleep(1);
        }

        //printf("http data: %s\n", returnData);

        code = internal_GetHTTPResponseCode(session, returnData);
        free(returnData);

        if(strcmp(code, "101") == 0)
        {
          DisC_Log(session->logLevel, session->logFileLocation, DISC_SEVERITY_NOTIFY, "Gateway handshake success");
          DisC_AddError(session, DISC_ERROR_NONE);
          returnval = DISC_ERROR_NONE;
          DisC_gateway_payload_t *internalPayload = NULL;

          //parse json now
          returnData = internal_ReadData(session, &returnDataLen, DISC_FALSE);
          while(returnData == NULL && (errno == EAGAIN || errno == EWOULDBLOCK))
          {
            returnData = internal_ReadData(session, &returnDataLen, DISC_FALSE);
            sleep(1);
          }
          jsonResponse = strstr(returnData, "{");
          char *replace = strrchr(jsonResponse, '}');
          replace++;
          *replace = '\0';
          //printf("json data: %s\n", jsonResponse);

          internalPayload = DisC_object_GenerateGatewayPayload(session, jsonResponse);

          printf("opcode: %d data: %s\n", internalPayload->op, internalPayload->d);
          if(internalPayload->op == DISC_OP_HELLO)
          {
            json_t *hello = json_loads(internalPayload->d, 0, NULL);
            session->DONOTSET_heartbeat_interval = json_integer_value(json_object_get(hello, "heartbeat_interval"));
            printf("Heartbeat interval: %d\n", session->DONOTSET_heartbeat_interval);
            json_decref(hello);
            //session->DONOTSET_heartbeat_interval = internalPayload->

            //send first heartbeat

          }
          else
          {
            //error
            DisC_Log(session->logLevel, session->logFileLocation, DISC_SEVERITY_ERROR, "Gateway did not reply HELLO");
            DisC_AddError(session, DISC_ERROR_CONNECTION);
            returnval = DISC_ERROR_CONNECTION;
          }


        }
        else
        {
          DisC_Log(session->logLevel, session->logFileLocation, DISC_SEVERITY_NOTIFY, "Could not handshake with gateway");
          DisC_AddError(session, DISC_ERROR_CONNECTION);
          returnval = DISC_ERROR_CONNECTION;
        }


        free(request);
        DisC_Log(session->logLevel, session->logFileLocation, DISC_SEVERITY_NOTIFY, "Connected to gateway");
        DisC_AddError(session, DISC_ERROR_NONE);
        returnval = DISC_ERROR_NONE;
      }

      free(finalWSURL);
    }
    else
    {
      DisC_Log(session->logLevel, session->logFileLocation, DISC_SEVERITY_ERROR, "Failed to parse JSON");
      DisC_AddError(session, DISC_ERROR_MEMORY);
      returnval = DISC_ERROR_MEMORY;
    }

  }
  else if(strcmp(code, "401") == 0)
  {
    DisC_Log(session->logLevel, session->logFileLocation, DISC_SEVERITY_WARNING, "Insufficient permissions for action");
    DisC_AddError(session, DISC_ERROR_PERMISSIONS);
    returnval = DISC_ERROR_PERMISSIONS;
  }
  else{
    DisC_Log(session->logLevel, session->logFileLocation, DISC_SEVERITY_WARNING, "HTTP error: %s: %s", code, readData);
    DisC_AddError(session, DISC_ERROR_GENERIC);
    returnval = DISC_ERROR_GENERIC;
  }

  free(code);
  free(readData);
  free(url);

  return returnval;
}

short DisC_gateway_DestroySession(DisC_session_t *session)
{
  DisC_Log(session->logLevel, session->logFileLocation, DISC_SEVERITY_NOTIFY, "Disconnecting %s from gateway...", session->internalName);
  BIO_free_all(session->DONOTSET_gateway_bio);
  SSL_CTX_free(session->DONOTSET_gateway_ctx);
}

short DisC_gateway_ListenAndManage(DisC_session_t *session)
{

}

//gateway event manage functions

short DisC_gateway_SendEvent(DisC_session_t *session, short OP, unsigned char *data, unsigned long dataLen)
{
  unsigned char *returnData;
  unsigned long returnDataLen = 0;
  json_t *root = json_object();

  json_object_set_new(root, "op", json_integer((int)OP));
  json_object_set_new(root, "d", json_loads(data, 0, NULL));//need to check if the data is even json data

  printf("Printevent: %s\n", json_dumps(root, JSON_INDENT(2)));

  internal_WriteData(session, json_dumps(root, 0), strlen(json_dumps(root, 0)));
  returnData = internal_ReadData(session, &returnDataLen, DISC_TRUE);
  while(returnData == NULL && (errno == EAGAIN || errno == EWOULDBLOCK))//TODO remove this because discord only sends ACK with heartbeats
  {
    //block until the ACK response
    returnData = internal_ReadData(session, &returnDataLen, DISC_TRUE);
    sleep(1);
  }
  //returnData[returnDataLen] == 0x00;
  printf("returned gateway event: %s\n", returnData);


  json_decref(root);


  DisC_Log(session->logLevel, session->logFileLocation, DISC_SEVERITY_NOTIFY, "Sent event to gateway");
  DisC_AddError(session, DISC_ERROR_NONE);
  return DISC_ERROR_NONE;
}

short DisC_gateway_Identify(DisC_session_t *session, short large_threshold, short shardnum, short shardmax, DisC_gateway_status_update_t *status_update)
{
  int returnval = 0;
  json_t *root = json_object();
  json_t *properties = json_object();
  json_t *presence = json_object();
  json_t *game = json_object();
  json_t *shards = json_array();

  json_object_set_new(root, "token", json_string(session->token));

  #ifdef _WIN32
  json_object_set_new(properties, "$os", json_string("windows"));
  if(session->logLevel == DISC_LOG_VERBOSE_AND_PRINT)
    DisC_Log(session->logLevel, session->logFileLocation, DISC_SEVERITY_NOTIFY, "Detected OS is windows");
  #elif __APPLE__
  json_object_set_new(properties, "$os", json_string("macOS"));
  if(session->logLevel == DISC_LOG_VERBOSE_AND_PRINT)
    DisC_Log(session->logLevel, session->logFileLocation, DISC_SEVERITY_NOTIFY, "Detected OS is mac OS");
  #elif __linux__
  json_object_set_new(properties, "$os", json_string("linux"));
  if(session->logLevel == DISC_LOG_VERBOSE_AND_PRINT)
    DisC_Log(session->logLevel, session->logFileLocation, DISC_SEVERITY_NOTIFY, "Detected OS is linux");
  #elif __unix__
  json_object_set_new(properties, "$os", json_string("unix"));
  if(session->logLevel == DISC_LOG_VERBOSE_AND_PRINT)
    DisC_Log(session->logLevel, session->logFileLocation, DISC_SEVERITY_NOTIFY, "Detected OS is unix");
  #else
  json_object_set_new(properties, "$os", json_string("unknown"));
  if(session->logLevel == DISC_LOG_VERBOSE_AND_PRINT)
    DisC_Log(session->logLevel, session->logFileLocation, DISC_SEVERITY_NOTIFY, "Detected OS is unknown");
  #endif
  json_object_set_new(properties, "$browser", json_string("DisC"));
  json_object_set_new(properties, "$device", json_string("DisC"));
  json_object_set_new(root, "properties", properties);

  json_object_set_new(root, "compress", json_boolean(DISC_FALSE));//TODO make a compression alternative option
  json_object_set_new(root, "large_threshold", json_integer((int)large_threshold));
  if(shardmax > 0)
  {
    json_array_append_new(shards, json_integer(shardnum));
    json_array_append_new(shards, json_integer(shardmax));
    json_object_set_new(root, "shard", shards);//if this isnt necessary, then make it only run if requiring shards by user TODO this is the issue
  }


  if(status_update->game.type != DISC_ACTIVITY_NOTHING)
  {
    json_object_set_new(game, "name", json_string(status_update->game.name));
    json_object_set_new(game, "type", json_integer(status_update->game.type));
    if(status_update->game.url != NULL)
      json_object_set_new(game, "url", json_string(status_update->game.url));
    json_object_set_new(presence, "game", game);
  }
  switch(status_update->status)
  {
    case DISC_STATUS_ONLINE:
      json_object_set_new(presence, "status", json_string("online"));
    break;
    case DISC_STATUS_DO_NOT_DISTURB:
      json_object_set_new(presence, "status", json_string("dnd"));
    break;
    case DISC_STATUS_IDLE:
      json_object_set_new(presence, "status", json_string("idle"));
    break;
    case DISC_STATUS_INVISIBLE:
      json_object_set_new(presence, "status", json_string("invisible"));
    break;
    case DISC_STATUS_OFFLINE:
      json_object_set_new(presence, "status", json_string("offline"));
    break;
  }
  if(status_update->since > 0)
    json_object_set_new(presence, "since", json_integer(status_update->since));
  else
    json_object_set_new(presence, "since", NULL);
  json_object_set_new(presence, "afk", json_boolean(status_update->afk));
  json_object_set_new(root, "presence", presence);

  //printf("printway: %s\n", json_dumps(root, JSON_INDENT(2)));

  //send the data to the gateway

  if(DisC_gateway_SendEvent(session, DISC_OP_IDENTIFY, json_dumps(root, 0), strlen(json_dumps(root, 0))) != DISC_ERROR_NONE)
  {//TODO before the rain knocks out power, I need to set this as an error case. This is a reminder for future me
    DisC_Log(session->logLevel, session->logFileLocation, DISC_SEVERITY_ERROR, "Could not identify with gateway");
    DisC_AddError(session, DISC_ERROR_CONNECTION);
    returnval = DISC_ERROR_CONNECTION;
  }
  else
  {
    DisC_Log(session->logLevel, session->logFileLocation, DISC_SEVERITY_NOTIFY, "Identified with gateway");
    DisC_AddError(session, DISC_ERROR_NONE);
    returnval = DISC_ERROR_NONE;
  }

  json_decref(game);
  json_decref(presence);
  json_decref(properties);
  json_decref(root);

  return returnval;
}
