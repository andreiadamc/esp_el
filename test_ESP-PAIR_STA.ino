#include <LinkedList.h>

#include "SSD1306.h"

// Initialize the OLED display using Wire library
SSD1306  display(0x3c, D1, D2);

extern "C" {
#include "user_interface.h"
#include "simple_pair.h"
#include <espnow.h>
}

#include <Arduino.h>
#include <stdarg.h>
#include <TimeLib.h>
#include "MercCntr3.h"

void p(char *fmt, ... ) {
  char buf[128]; // resulting string limited to 128 chars
  va_list args;
  va_start (args, fmt );
  vsnprintf(buf, 128, fmt, args);
  va_end (args);
  /*display.clear();
    display.print(buf);
    display.drawLogBuffer(0, 0);
    display.display();*/
  Serial.print(buf);
}

void p1(char *fmt, ... ) {
  char buf[128]; // resulting string limited to 128 chars
  va_list args;
  va_start (args, fmt );
  vsnprintf(buf, 128, fmt, args);
  va_end (args);
  display.clear();
    display.print(buf);
    display.drawLogBuffer(0, 0);
    display.display();
  //Serial.print(buf);
}

void _showtime() {
  p("Time: %d:%d:%d %d-%d-%d\n", hour(), minute(), second(), day(), month(), year());
  p1("Time: %d:%d:%d %d-%d-%d\n", hour(), minute(), second(), day(), month(), year());
  
}

void init_PrintSystem() {
  display.init();
  display.flipScreenVertically();
  display.setLogBuffer(5, 30);
}

t_macid my_sta_mac = {0, 0, 0, 0, 0, 0};
t_macid my_ap_mac = {0, 0, 0, 0, 0, 0};


volatile bool in_sta_mode = false;
volatile bool in_ap_mode = false;

unsigned long lastrunAP, lastrunSTA, lastrunCHECKMSG, lastrunSENDMSG, lastrunDATASEND;
boolean initESPNOWDone = false;

void printMacAddress(t_macid macaddr) {
  Serial.print("{");
  for (int i = 0; i < 6; i++) {
    //Serial.print("0x");
    Serial.print(macaddr[i], HEX);
    if (i < 5) Serial.print(':');
  }
  Serial.println("}");
}

void printMacAddress1(uint8_t* macaddr) {
  Serial.print("{");
  for (int i = 0; i < 6; i++) {
    //Serial.print("0x");
    Serial.print(macaddr[i], HEX);
    if (i < 5) Serial.print(':');
  }
  Serial.println("}");
}

static u8 tmpkey[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
                       };

/* since the ex_key transfer from AP to STA, so AP's ex_key must be set */
static u8 ex_key[16] = {0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88,
                        0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00
                       };

void ICACHE_FLASH_ATTR
show_key(u8 *buf, u8 len) {
  u8 i;
  for (i = 0; i < len; i++)
    p("%02x,%s", buf[i], (i % 16 == 15 ? "\n" : " "));
}

/*static*/ void ICACHE_FLASH_ATTR
scan_done(void *arg, STATUS status) {
  int ret;
  //_showtime();
  //p("SP: Scan done\n");
  if (status == OK) {
    //p("SP: status OK\n");
    struct bss_info *bss_link = (struct bss_info *)arg;
    if (bss_link == NULL) p("SP: bss_link is NULL\n");
    while (bss_link != NULL) {
      if (PeersMACList.size() == C_MAX_PEER_NUMBER) {
        p("SP: Max number of peers! Scan canceled.\n");
        break;
      }

      String ssid = String(reinterpret_cast<char*>(bss_link->ssid));

      String rssi = String(bss_link->rssi);
      String auth_mode = String(bss_link->authmode);
      p("SP: %s ( %s, %s, %d)\n", ssid.c_str(),
        rssi.c_str(), auth_mode.c_str(), bss_link->channel);
      p("SP: is simple %d\n", bss_link->simple_pair);
      p("SP: index = %d\n", my_indexofpeer(bss_link->bssid));
      if (bss_link->simple_pair && (my_indexofpeer(bss_link->bssid) < 0)) {
        p("SP: mac %02x:%02x:%02x:%02x:%02x:%02x Ready!\n",
          bss_link->bssid[0], bss_link->bssid[1], bss_link->bssid[2],
          bss_link->bssid[3], bss_link->bssid[4], bss_link->bssid[5]);
        simple_pair_set_peer_ref(bss_link->bssid, tmpkey, NULL);
        ret = simple_pair_sta_start_negotiate();
        if (ret)
          p("SP: STA start NEG Failed\n");
        else {
          p("SP: STA start NEG OK\n");
          p_macid newpeer = new t_macid();
          memcpy(newpeer, bss_link->bssid, 6);
          PeersMACList.add(newpeer);
          //Добавляем peer
          ret = esp_now_add_peer(bss_link->bssid, ESP_NOW_ROLE_SLAVE, ESP_NOW_CHANNEL, (u8*)&ESP_KEY, ESP_KEY_LEN);
          if (!ret) //=0 OK
            p("SP: Add peer err: %d\n", ret);
          else
            p("SP: Peer added\n");
        }
        //AA break;
      }
      //p("SP: next peer\n");
      bss_link = bss_link->next.stqe_next;//->next;
    }
  } else {
    p("SP: err, scan status %d\n", status);
  }
  //delay(200);
  //_showtime();
  simple_pair_deinit();
  //_showtime();
  //p("SP: deinit\n");
  in_sta_mode = false;
}


void ICACHE_FLASH_ATTR
sp_status_sta(u8 *sa, u8 status) {
  switch (status) {
    case  SP_ST_STA_FINISH:
      simple_pair_get_peer_ref(NULL, NULL, ex_key);
      p("SP: STA FINISH, Ex_key ");
      //p1("SP: STA FINISH, Ex_key ");
      show_key(ex_key, 16);
      /* TODO: Try to use the ex-key communicate with AP, for example use ESP-NOW */
      /* if test ok , deinit simple pair */
      //AA simple_pair_deinit();
      //lets continue searching ap
      break;
    case SP_ST_STA_AP_REFUSE_NEG:
      /* AP refuse , so try simple pair again  or scan other ap*/
      p("SP: Recv AP Refuse\n");
      //p1("SP: Recv AP Refuse\n");
      simple_pair_state_reset();
      simple_pair_sta_enter_scan_mode();
      wifi_station_scan(NULL, scan_done);
      break;
    case SP_ST_WAIT_TIMEOUT:
      /* In negotiate, timeout , so try simple pair again */
      p("SP: Neg Timeout\n");
      simple_pair_state_reset();
      simple_pair_sta_enter_scan_mode();
      wifi_station_scan(NULL, scan_done);
      break;
    case SP_ST_SEND_ERROR:
      p("SP: Send Error\n");
      /* maybe the simple_pair_set_peer_ref() haven't called, it send to a wrong mac address */

      break;
    case SP_ST_KEY_INSTALL_ERR:
      p("SP: Key Install Error\n");
      /* 1. maybe something argument error.
         2. maybe the key number is full in system*/

      /* TODO: Check other modules which use lots of keys
                           Example: ESPNOW and STA/AP use lots of keys */
      break;
    case SP_ST_KEY_OVERLAP_ERR:
      p("SP: Key Overlap Error\n");
      /* 1. maybe something argument error.
         2. maybe the MAC Address is already use in ESP-NOW or other module
            the same MAC Address has multi key*/

      /* TODO: Check if the same MAC Address used already,
                           Example: del MAC item of ESPNOW or other module */
      break;
    case SP_ST_OP_ERROR:
      p("SP: Operation Order Error\n");
      /* 1. maybe the function call order has something wrong */

      /* TODO: Adjust your function call order */
      break;
    default:
      p("SP: Unknown Error\n");
      break;
  }
}

void ICACHE_FLASH_ATTR
sp_status_ap(u8 *sa, u8 status) {
  switch (status) {
    case  SP_ST_AP_FINISH:
      simple_pair_get_peer_ref(NULL, NULL, ex_key);
      p("SP: AP FINISH\n");
      //p1("SP: AP FINISH\n");
      /* TODO: Wait STA use the ex-key communicate with AP, for example use ESP-NOW */
      /* if test ok , deinit simple pair */
      //simple_pair_deinit();
      break;
    case SP_ST_AP_RECV_NEG:
      /* AP recv a STA's negotiate request */
      p("SP: Recv STA Negotiate Request\n");
      //p1("SP: Recv STA Negotiate Request\n");
      /* set peer must be called, because the simple pair need to know what peer mac is */
      simple_pair_set_peer_ref(sa, tmpkey, ex_key);
      /* TODO:In this phase, the AP can interaction with Smart Phone,
                     if the Phone agree, call start_neg or refuse */
      simple_pair_ap_start_negotiate();
      //simple_pair_ap_refuse_negotiate();
      /* TODO:if refuse, maybe call simple_pair_deinit() to ending the simple pair */
      break;
    case SP_ST_WAIT_TIMEOUT:
      /* In negotiate, timeout , so re-enter in to announce mode*/
      p("SP: Neg Timeout\n");
      simple_pair_state_reset();
      simple_pair_ap_enter_announce_mode();
      break;
    case SP_ST_SEND_ERROR:
      p("SP: Send Error\n");
      /* maybe the simple_pair_set_peer_ref() haven't called, it send to a wrong mac address */
      break;
    case SP_ST_KEY_INSTALL_ERR:
      p("SP: Key Install Error\n");
      /* 1. maybe something argument error.
         2. maybe the key number is full in system*/
      /* TODO: Check other modules which use lots of keys
                           Example: ESPNOW and STA/AP use lots of keys */
      break;
    case SP_ST_KEY_OVERLAP_ERR:
      p("SP: Key Overlap Error\n");
      /* 1. maybe something argument error.
         2. maybe the MAC Address is already use in ESP-NOW or other module
            the same MAC Address has multi key*/

      /* TODO: Check if the same MAC Address used already,
                           Example: del MAC item of ESPNOW or other module */
      break;
    case SP_ST_OP_ERROR:
      p("SP: Operation Order Error\n");
      /* 1. maybe the function call order has something wrong */
      /* TODO: Adjust your function call order */
      break;
    default:
      p("SP: Unknown Error\n");
      break;
  }
}

void ICACHE_FLASH_ATTR
deinit_sp(void) {
  p("SP: Deinit SP start\n");
  simple_pair_deinit();
  unregister_simple_pair_status_cb();
  in_ap_mode = false;
  in_sta_mode = false;
  p("SP: Deinit SP done\n");
}

void ICACHE_FLASH_ATTR
close_wifi(void) {
  int ret = wifi_station_disconnect(); // возвращает bool , т.е. 1 - true 0 - false
  wifi_set_opmode_current(NULL_MODE);
  p("WiFi disconnected 1-OK 0-Err %d\n", ret);
  delay(20);
}

int ICACHE_FLASH_ATTR
init_sp_sta(void) {
  int ret;
  p("STA: init start\n");
  p1("STA ON\n");
  close_wifi();
  wifi_set_opmode(STATION_MODE);
  delay(20);
  /* init simple pair */
  ret = simple_pair_init();
  if (ret) {
    p("STA: init error, %d\n", ret);
  } else {
    /* register simple pair status callback function */
    ret = register_simple_pair_status_cb(sp_status_sta);
    if (ret) {
      p("STA: register status cb error, %d\n", ret);
    } else {
      p("STA: STA Enter Scan Mode ...\n");
      ret = simple_pair_sta_enter_scan_mode();
      if (ret) {
        p("STA: STA Enter Scan Mode Error, %d\n", ret);
      } else {
        /* scan ap to searh which ap is ready to simple pair */
        p("STA: STA Scan AP ...\n");
        struct scan_config config;
        memset(&config, 0, sizeof(config));
        config.ssid = 0;
        config.bssid = 0;
        config.channel = 0;
        config.show_hidden = 1;
        ret = wifi_station_scan(NULL, scan_done);//&config
        if (!ret) {
          p("STA: WiFi err %d\n", ret);
          ret = 11;
        } else ret = 0;
        p("STA: Init done\n");
      }
    }
  }
  in_sta_mode = !ret;
  if (ret) {
    p("STA: deinit sp err: %d", ret);
    deinit_sp();
  }
  lastrunSTA = now();
  return ret;
}

int ICACHE_FLASH_ATTR
init_sp_ap(void) {
  int ret;
  p("AP: Init start\n");
  p1("AP ON\n");
  close_wifi();
  wifi_set_opmode(SOFTAP_MODE);
  delay(20);
  /* init simple pair */
  ret = simple_pair_init();
  if (ret) {
    p("AP: init error, %d\n", ret);
  } else {
    /* register simple pair status callback function */
    ret = register_simple_pair_status_cb(sp_status_ap);
    if (ret) {
      p("AP: register status cb error, %d\n", ret);
    } else {
      p("AP: AP Enter Announce Mode ...\n");
      /* ap must enter announce mode , so the sta can know which ap is ready to simple pair */
      ret = simple_pair_ap_enter_announce_mode();
      if (ret) {
        p("AP: AP Enter Announce Mode Error, %d\n", ret);
      }
    }
  }
  p("AP: Init done\n");
  in_ap_mode = !ret;
  if (ret) {
    p("AP: deinit sp err: %d", ret);
    deinit_sp();
  }
  lastrunAP = now();
  return ret;
}

p_esp_packet incomingmsg1;

void deinit_EspNow() {
  initESPNOWDone = false;
  esp_now_unregister_recv_cb();
  esp_now_unregister_send_cb();
  esp_now_deinit();
}

void init_EspNow() {
  p("ESPNow: init start\n");
  if (wifi_set_opmode(STATIONAP_MODE)) //STATION_MODE STATIONAP_MODE
    p("ESPNow: set mode done\n");
  delay(10);
  //wifi_set_channel(ESP_NOW_CHANNEL);
  p("ESPNow: init start Channel = %d\n", wifi_get_channel());
  if (esp_now_init()) {
    p("ESPNow: init failed\n");
    ESP.restart();
    delay(200);
    return;
  }
  if (!esp_now_set_self_role(ESP_NOW_ROLE_COMBO)) { //=0 OK
    esp_now_register_recv_cb([](uint8_t *mac, uint8_t *data, uint8_t len) { //при получении
      incomingmsg1 = new t_esp_packet();
      memcpy(incomingmsg1, data, sizeof(t_esp_packet));
      if (check_msg_crc(incomingmsg1)) { // проверяем CRC если все ок сохраняем
        InMsgList.add(incomingmsg1);
      }
      p("ESPNow: Receive callback msg from MAC = ");
      printMacAddress1(mac);
      p("ESPNow: Recieved data lenght = %d\n", len);
    });

    esp_now_register_send_cb([](uint8_t* mac, uint8_t status) {
      p("ESPNow: Send to MAC = ");
      printMacAddress1(mac);
      p("ESPNow: Result (0=0K - 1=ERROR): %d\n", status);
    });

    initESPNOWDone = true;
  } else p("ESPNow: init error\n");

  p("ESPNow: init done\n");
}

uint8_t my_indexofdevice(void* amac) {
  for (int i = 0; i < DevicesList.size(); i++) {
    if (memcmp(DevicesList.get(i), amac, 6)) {
      p("mac found %d", i);
      return i;
    }
  }
  p("mac not found!");
  return -1;
}

void init_DeviceList() {
  p("DvcList: init start\n");
  p_device_data tmp_dvc = new t_device_data();
  DevicesList.add(tmp_dvc); //должен быть первым элементом в списке
  memcpy(&tmp_dvc->actual_data.mac, &my_sta_mac, 6);
  v_local_counters = &tmp_dvc->actual_data.counters;
  p("DvcList: init done\n");
}

void clear_DeviceList() {
  p("DvcList: clear\n");
  while (DevicesList.size() > 1) {
    p_device_data tmp_dvc = DevicesList.pop();
    delete tmp_dvc;
  }
}

void ParsingIncomingMsgs() {
  p_device_data tmp_dvc;
  p("MSG CHECK: Start\n");
  if (InMsgList.size() > 0) {
    p("MSG CHECK: Has some msgs to parse\n");
    p_esp_packet tmp_pkg = InMsgList.pop();
    if (tmp_pkg->msg_type == msgtCntData) {
      p("MSG CHECK: New counter data arrived\n");
      // копируем значения в
      int i = my_indexofdevice(&(*tmp_pkg).mac);
      if (i != 0) { // в первом содержатся данные этого уст-ва

        if (i == -1) {   // новое устройство
          p("MSG CHECK: New device MAC = ");
          printMacAddress((*tmp_pkg).mac);
          tmp_dvc = new t_device_data();
          i = DevicesList.add(tmp_dvc);
        } else {
          p("MSG CHECK: Device already in the list\n");
          tmp_dvc = DevicesList.get(i);
        }
        p("MSG CHECK: Data arrived from MAC= ");
        printMacAddress((*tmp_pkg).mac);
        p("MSG CHECK: Node time = %d", tmp_pkg->nodetime);
        if (tmp_dvc->actual_data.last_remote_time < tmp_pkg->nodetime) {
          memcpy(tmp_dvc, tmp_pkg, sizeof(t_device_data1));
          tmp_dvc->last_local_time = now();
          p("MSG CHECK: Data age is %d\n", tmp_dvc->actual_data.last_remote_time);
          p("MSG CHECK: Data cnt_lo = %d\n", tmp_dvc->actual_data.counters.F_cnt_lo);
          p("MSG CHECK: Data cnt_hi1 = %d\n", tmp_dvc->actual_data.counters.F_cnt_hi1);
          p("MSG CHECK: Data cnt_hi2 = %d\n", tmp_dvc->actual_data.counters.F_cnt_hi2);
        } else p("MSG CHECK: Saved Node time is newer = %d\n", tmp_dvc->actual_data.last_remote_time);

        // здесь нужно отправить этот пакет дальше по списку устройств
        // кроме того от кого получено и кроме источника
        tmp_pkg->timetolive--;
        if (tmp_pkg->timetolive) {
          for (uint8_t j = 0; j < PeersMACList.size(); j++) {
            p_macid tmpmac = PeersMACList.get(j);
            if ((!memcmp(tmpmac, &tmp_pkg->mac, MAC_SIZE)) && (!memcmp(tmpmac, &tmp_pkg->scr_mac, MAC_SIZE))) {
              // если не источник и не отправитель
              p_esp_packet out_msg = new t_esp_packet();
              memcpy(out_msg, tmp_pkg, sizeof(t_esp_packet));
              memcpy(&out_msg->mac, tmpmac, MAC_SIZE);//кому
              OutMsgList.add(out_msg);
            }
          }
        }
      } else p("MSG CHECK: This's a data for the same device ;-) ");
    } else if (tmp_pkg->msg_type == msgtSyncTime) {
      if (last_synctime_msgid != tmp_pkg->msgid) {
        p("MSG CHECK: Update old datetime %d to %d\n", now(), 2);//tmp_pkg->d_time
        setTime(tmp_pkg->d_time);
        last_synctime_msgid = tmp_pkg->msgid;
      } else p("MSG CHECK: Update datetime canceled. Same msgid %d\n", tmp_pkg->msgid);
    } else if (tmp_pkg->msg_type == msgtCntClear) {
      if (memcmp(&my_sta_mac, &tmp_pkg->scr_mac, MAC_SIZE)) {
        p("MSG CHECK: Lets clear the counter!\n");
        tmp_dvc = DevicesList.get(0);
        tmp_dvc->last_local_time = now();
        tmp_dvc->actual_data.last_remote_time = 0;
        tmp_dvc->actual_data.counters.F_cnt_lo = 0;
        tmp_dvc->actual_data.counters.F_cnt_hi1 = 0;
        tmp_dvc->actual_data.counters.F_cnt_hi2 = 0;
      }
    } else if (tmp_pkg->msg_type == msgtCntSet) {

    } else if (tmp_pkg->msg_type == msgtSetWebSrv) {

    }
    delete tmp_pkg;
  }
  p("MSG CHECK: End\n");
}

void CreateDataMsg() {
  p("CreateDataMsg: Start\n");
  p_esp_packet out_msg = new t_esp_packet();
  //Создание сообщения о состоянии счетчиков
  memset(&out_msg->mac, 0, MAC_SIZE);//кому - всем
  //memcpy(&out_msg->mac, &my_sta_mac, MAC_SIZE);//от кого сообщение или кому при отправке
  out_msg->nodetime = now(); //время отправки у первого отправителя
  memcpy(&out_msg->d_counters, &v_local_counters, sizeof(t_merc_data));
  memcpy(&out_msg->scr_mac, &my_sta_mac, MAC_SIZE); // первый отправитель сообщения
  //uint32_t msgid; // уникальный номер сообщения при первой отправке
  out_msg->msg_type = msgtCntData; // тип сообщения
  out_msg->timetolive = MSG_TIMETOLIVE; // время жизни сообщения
  out_msg->msg_crc = crc_16((byte*)out_msg, sizeof(t_esp_packet) - 4);
  OutMsgList.add(out_msg);
  p("CreateDataMsg: End\n");
}

void SendingMsgs() {
  p("MSG SEND: Start\n");
  int ret = false;
  // отправляем сообщение из списка каждому адресу в списке peer
  if (OutMsgList.size() > 0) {
    p("MSG SEND: msg count %d\n", OutMsgList.size());
    p_esp_packet tmp_pkg = OutMsgList.get(0);
    if (PeersMACList.size() > 0) {
      //Нужно отсылать всем если не не указан mac
      if (memcmp(&tmp_pkg->mac, &NULL_MAC, MAC_SIZE)) { //!=0
        if (esp_now_is_peer_exist((u8*)&tmp_pkg->mac) < 1) {
          p("MSG SEND: Peer not found\n");
          //ret = esp_now_add_peer(&tmp_pkg->mac[0], ESP_NOW_ROLE_SLAVE, ESP_NOW_CHANNEL, &ESP_KEY[0], ESP_KEY_LEN);
          //if (!ret)
          //    p("MSG SEND: Add peer err: %d\n", ret);
        }
        if (!ret) ret = esp_now_send((u8*)&tmp_pkg->mac, (u8*)tmp_pkg, t_esp_packet_size);
      } else ret = esp_now_send(NULL, (u8*)tmp_pkg, t_esp_packet_size);
    }
    if (!ret)
      OutMsgList.remove(0);
    else
      p("MSG SEND: Error send pkg: %d\n", ret);
  }
  p("MSG SEND: End\n");
}

void setup() {
  // put your setup code here, to run once:
  close_wifi();
  setTime(1499833600 + RANDOM_REG32/4);
  delay(2000);
  Serial.begin(115200);
  delay(1000);
  Serial.println("Start print system");
  init_PrintSystem();
  Serial.println("Print system running");
  _showtime();

  uint8_t mac[6];
  wifi_get_macaddr(SOFTAP_IF, mac);
  p("AP MAC: ");
  printMacAddress1(mac);
  wifi_get_macaddr(STATION_IF, mac);
  p("STA MAC: ");
  printMacAddress1(mac);
  init_DeviceList();
  v_local_counters->F_cnt_hi2 = 1;
  v_local_counters->F_cnt_hi1 = 1;
  v_local_counters->F_cnt_lo = 1;

  wifi_set_channel(ESP_NOW_CHANNEL);

  init_EspNow();
}

void loop() {
  delay(100);
  if (in_ap_mode && (numberOfMinutes(now() - lastrunAP) > RUN_AP_INTERVAL_MINUTES)) {
    p("Minutes AP Now %d and %d", numberOfMinutes(now()) , numberOfMinutes(lastrunAP));
    p("Deinit SP due to time\n");
    p1("Deinit SP due to time\n");
    deinit_sp();
  } else if (!in_ap_mode && !in_sta_mode && (numberOfMinutes(now()-lastrunAP) > RUN_AP_FREQ_MINUTES)) {
    init_sp_ap();
    //} else if (!in_ap_mode && !in_sta_mode && !initESPNOWDone) {
    //init_EspNow();
  } else if (!in_ap_mode && !in_sta_mode && (numberOfMinutes(now()-lastrunSTA) > RUN_STA_FREQ_MINUTES)) {
    init_sp_sta();
  } 

  if (numberOfMinutes(now() - lastrunCHECKMSG) > CHECK_MSG_INTERVAL_MINUTES) {
    lastrunCHECKMSG = now();
    ParsingIncomingMsgs();
  } 
if (numberOfMinutes(now()- lastrunDATASEND) > DATA_SEND_INTERVAL_MINUTES) {
    v_local_counters->F_cnt_hi1 += random(5);
    v_local_counters->F_cnt_lo += random(100);
    lastrunDATASEND = now();
    CreateDataMsg();
  } 
if (numberOfMinutes(now()-lastrunSENDMSG) > SEND_MSG_INTERVAL_MINUTES) {
    lastrunSENDMSG = now();
    SendingMsgs();
  }
}
