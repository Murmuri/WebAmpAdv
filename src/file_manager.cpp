#include "file_manager.h"

AudioLinks audioLinks[MAX_LINKS];

AudioLinks defaultAudioLinks[] = {
  {"Staroe Radio Music 128", "https://www.staroeradio.ru/radio/music128"},
  {"Staroe Radio Kids 128", "https://www.staroeradio.ru/radio/detskoe128"},
  {"Staroe Radio 128", "https://www.staroeradio.ru/radio/ices128"},
  {"Staroe Radio Music 64", "https://www.staroeradio.ru/radio/music64"},
  {"Staroe Radio Kids 64", "https://www.staroeradio.ru/radio/detskoe64"},
  {"Staroe Radio 64", "https://www.staroeradio.ru/radio/ices64"},
  {"Staroe Radio Music 32", "https://www.staroeradio.ru/radio/music32"},
  {"Staroe Radio Kids 32", "https://www.staroeradio.ru/radio/detskoe32"},
  {"Staroe Radio 32", "https://www.staroeradio.ru/radio/ices32"},
  {"JAZZ GROOVE", "http://audio-edge-5bkfj.fra.h.radiomast.io/8a384ff3-6fd1-4e5d-b47d-0cbefeffe8d7"},
  {"FLAIX FM", "http://nodo02-cloud01.streaming-pro.com:8000/flaixfm.mp3"},
  {"LUXFUNK RADIO", "http://188.165.11.30:5000/luxfunkradio.mp3"},
  {"Shaq Fu Radio", "http://ais-edge148-pit01.cdnstream.com/2092_128.mp3"},
  {"Punkirratia.Net Euskadi", "http://punkirratia.net:8000/punk"},
  {"La Rielera de Aguascalientes", "http://ec3.yesstreaming.net:2810/stream"},
  {"Japan Only Hits", "http://ais-edge91-dal03.cdnstream.com/2629_128.mp3"},
  {"KRDP Jazz Radio", "http://cp10.shoutcheap.com:8367/krdp_jazz"},
  {"Crucial Velocity Radio", "http://ais-edge94-nyc04.cdnstream.com/1369_128"},
  {"LS4 Radio Continental 590", "http://edge05.radiohdvivo.com:9126/live argentina"},
  {"Radio Tango", "http://ais-edge148-pit01.cdnstream.com/2202_128.mp3 Alemania"},
  {"Opera katolikus Radio", "http://www.katolikusradio.hu:9000/opera "},
  {"RAI Radio", "http://icestreaming.rai.it/1.mp3 Italia"},
  {"EU URRS aka Russia", "http://europarussia.ru:8004/live "},
  {"Make 80s great AGAIN", "http://ais-edge147-dal03.cdnstream.com/2281_128.mp3"},
  {"Twente Gold - Fallout", "http://c18.radioboss.fm:8403/autodj VAULT"},
  {"Radio Galaxy - PipRaDI0", "http://c15.radioboss.fm:8078/autodj "},
  {"4 eVeR X-MaS", "http://ais-edge114-dal02.cdnstream.com/1102_128 "},
  {"Klasszik Rádió 92.1", "http://s04.diazol.hu:9600/live.mp3"},
  {"DreadLOCK Jah Radio", "http://ic1.dread.fast-serv.com/dreadlockradio128 "},
  {"Radio SuperFLY", "http://stream01.superfly.fm/live "},
  {"RTHK Radio", "http://stm.rthk.hk:80/radio1 "},
  {"Los 40 - Argentina", "http://edge01.radiohdvivo.com:9006/live "},
  {"Le Son Parisien", "http://stream.lesonparisien.com/hi "},
  {"Pure FM - Berlin", "http://s4.radionetz.de/purefm-bln.mp3 "},
  {"Dubai Radio", "http://c20.radioboss.fm:8543/live "},
  {"Radio Soyuz Книга вслух", "http://65.109.84.248:8100/soyuzfm-192.mp3"},
  {"Radio DEFCON hacker station ", "http://ice6.somafm.com/defcon-128-mp3 "},
  {"KAH Radio Gansta", "http://ec5.yesstreaming.net:2170/stream "},
  {"Radio Patmos", "http://s3.yesstreaming.net:7011/radio "},
  {"Polskie Radio OLSZTYN ", "http://rostr1.radioolsztyn.pl:8000/RO.mp3 "},
  {"COOL radio | Serbia", "http://live.coolradio.rs/cool192 "},
  {"RADIO APOLLON 1242 AM MW ", "http://webinternetradio.eu:7026/autodj "},
  {"Radio DIL india", "http://us3.streamingpulse.com:7026/radiodil2"},
  {"punkrockers-radio ", "http://stream.punkrockers-radio.de:8000/prr.mp3"},
  {"K-ROCK Radio", "http://ice10.fluidstream.net:8080/ric.mp3"},
  {"Radio Swiss Schlager", "http://relay.publicdomainradio.org/swiss_schlager.mp3"},
  {"Radio METEOR Belgium", "http://icecast.movemedia.be:8000/meteor"},
  {"Radio HARANA Manila", "http://ice.streams.ovh:1690/stream"},
  {"Radio Bremen", "http://icecast.radiobremen.de/rb/bremenvier/live/mp3/64/stream.mp3"},
};

SemaphoreHandle_t sdMutex = NULL;

uint8_t linksCount = 0;
uint8_t currentLinkIndex = 0;

bool initSDCard() {
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI);

    if (!SD.begin(SD_CS)) {
        Serial.println("ERROR: SD Mount Failed!");
        return false;
    }
    
    uint8_t cardType = SD.cardType();

    Serial.println("SD Card initialized successfully");
    Serial.printf("SD Card Type: %s\n", 
        cardType == CARD_MMC ? "MMC" :
        cardType == CARD_SD ? "SDSC" :
        cardType == CARD_SDHC ? "SDHC" : "UNKNOWN");
    Serial.printf("SD Card Size: %lluMB\n", SD.cardSize() / (1024 * 1024));
    
    return true;
}

void loadDefaultLinks() {
  int n = sizeof(defaultAudioLinks) / sizeof(defaultAudioLinks[0]);

  for (int i = 0; i < n; i++) {
    audioLinks[i].name = defaultAudioLinks[i].name;
    audioLinks[i].url = defaultAudioLinks[i].url;
    linksCount++;
  }
}

void getLinksList() {
  if (!SD.begin()) {
    loadDefaultLinks();
    return;
  }

  File file = SD.open("/webamp/link_list.txt");

  if (!file) {
    loadDefaultLinks();
    return;
  }

  linksCount = 0;
  
  while (file.available() && linksCount < MAX_LINKS) {
    String line = file.readStringUntil('\n');
    line.trim();
    line.replace("\r", "");
    
    if (line.length() == 0) {
      continue;
    }
    
    int commaIndex = line.indexOf(',');

    if (commaIndex > 0) {
        String name = line.substring(0, commaIndex);
        String url = line.substring(commaIndex + 1);
        
        name.trim();
        url.trim();
        
        if (
          name.length() > 0 && url.length() > 0 && 
          name.length() < MAX_NAME_LENGTH && 
          url.length() < MAX_URL_LENGTH
        ) {
            audioLinks[linksCount].name = name;
            audioLinks[linksCount].url = url;
            
            linksCount++;
        }
    }
  }

  file.close();

  if (linksCount == 0) {
    loadDefaultLinks();
  }
}

String getFileName(uint8_t index) {
    if (index >= linksCount) {
      return "";
    }
    
    String lName = audioLinks[index].name;

    return lName;
}