#define C_MAX_PEER_NUMBER 5
#define RUN_STA_FREQ_MINUTES 5
#define RUN_AP_FREQ_MINUTES 3
#define RUN_AP_INTERVAL_MINUTES 2
#define CHECK_MSG_INTERVAL_MINUTES 4
#define SEND_MSG_INTERVAL_MINUTES 3
#define DATA_SEND_INTERVAL_MINUTES 2
#define ESP_NOW_CHANNEL 3
#define MSG_TIMETOLIVE 10

uint8_t ESP_KEY[16] = {3,255,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
const static uint8_t ESP_KEY_LEN = sizeof(ESP_KEY);
const uint8_t NULL_MAC[6] = {0,0,0,0,0,0};

typedef struct  {
  uint16_t F_cnt_hi2;//0..65535 2 bytes здесь храним целые киловаты
  uint16_t F_cnt_hi1;//0..65535 2 bytes здесь храним целые киловаты
  uint16_t F_cnt_lo;//0..65535 2 bytes здесь храним кол-во импульсов
} t_merc_data, *p_merc_data;

typedef enum t_esp_packet_type {
  msgtNone = 0,
  msgtCntData, msgtSyncTime, msgtCntClear, msgtCntSet, msgtSetWebSrv
  //1 - пакет с данными для сохранения (пересылается всем) 2 - пакет с точным временем (всем)
  //3 - обнулить счетчик (всем пока не будет найден получатель) 
  //4 - установить счетчик (всем пока не будет найден получатель)
  //5 - запустить webserver на 10 мин
};

typedef struct {
  uint8_t& operator[](int i) {
    return byte[i];
  }
  uint8_t byte[6];
} t_macid, *p_macid;

#define MAC_SIZE sizeof(t_macid)

typedef struct {
  t_macid mac; //от кого сообщение или кому при отправке
  uint32_t nodetime; //время отправки у первого отправителя
  union {
    t_macid d_mac;
    uint32_t d_time;
    t_merc_data d_counters;
  };
  t_macid scr_mac; // первый отправитель сообщения
  uint32_t msgid; // уникальный номер сообщения при первой отправке
  t_esp_packet_type msg_type; // тип сообщения
  uint8_t timetolive; // время жизни сообщения
  uint32_t msg_crc;
} t_esp_packet, *p_esp_packet;

typedef struct  {
  t_macid mac;
  uint32_t last_remote_time;
  t_merc_data counters;
} t_device_data1, *p_device_data1;

#define t_esp_packet_size sizeof(t_esp_packet)

typedef struct  {
  t_device_data1 actual_data;
  uint32_t last_local_time;
} t_device_data, *p_device_data;

p_merc_data v_local_counters;
uint32_t last_synctime_msgid = 0; //ид последнего обновления времени

LinkedList<p_macid> PeersMACList = LinkedList<p_macid>(); // Только список связанных уст-в
LinkedList<p_device_data> DevicesList = LinkedList<p_device_data>(); // Список всех связанных уст-в
LinkedList<p_esp_packet> InMsgList = LinkedList<p_esp_packet>(); // Список принятых сообщений
LinkedList<p_esp_packet> OutMsgList = LinkedList<p_esp_packet>(); // Список сообщений к отправке

const static PROGMEM prog_uint32_t crc_table[16] = {
  0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
  0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
  0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
  0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
};


unsigned long crc_update(unsigned long crc, byte data)
{
  byte tbl_idx;
  tbl_idx = crc ^ (data >> (0 * 4));
  crc = pgm_read_dword_near(crc_table + (tbl_idx & 0x0f)) ^ (crc >> 4);
  tbl_idx = crc ^ (data >> (1 * 4));
  crc = pgm_read_dword_near(crc_table + (tbl_idx & 0x0f)) ^ (crc >> 4);
  return crc;
}

unsigned long crc_16(byte *s, byte sz)
{
  unsigned long crc = ~0L;
  while (sz) {
    crc = crc_update(crc, *s++);
    sz--;
  }
  crc = ~crc;
  return crc;
}

int check_msg_crc(p_esp_packet amsg){
  return (amsg->msg_crc == crc_16((byte*)amsg, sizeof(t_esp_packet)-4));
}

int my_indexofpeer(void* amac)
{
  for (int i = 0; i < PeersMACList.size(); i++) {
    if (memcmp(PeersMACList.get(i), amac, 6)) return i;
  }
  return -1;
}
