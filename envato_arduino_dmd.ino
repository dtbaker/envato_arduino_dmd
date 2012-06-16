/**
  * Arduino + DMD sales notification script
  * Author: dtbaker
  * Website: https://github.com/dtbaker/envato_arduino_dmd
  */


// TODO: move this out to an include file so we can commit changes via git easier.
const char envato_username[] = "USERNAME_HERE";
const char envato_api_key[] = "API_KEY_HERE";
#define BUZZPIN 2 // which pin is the piezo buzzer connected to (that buzzes when we make a sale)
#define MOTORPIN 2 // which pin is our little motor connected to (that spins when we make a sale)
/* C QUESTION: what would the difference between 'define' and 'long' for this check_interval: */
long check_interval = 120; // seconds
#define INC_AMOUNT 1 // when we count up, count up by this amount each time (eg: 1, 5, 10). 1 is fine.
#define enableSerial false


// ethernet:
#include <SPI.h>
#include <Ethernet.h>
// dmd:
#include <DMD.h>
#include <TimerOne.h>  
#include "Arial14.h"
#define da 1
#define dd 1
#define DMD_TOP_CHAR 1
#define do_dmd_detach_during_network true // just testing trying to fix the blinking DMD display during network operations.
DMD dmd(da,dd);
void ScanDMD(){ 
  dmd.scanDisplayBySPI();
}


// ethernet config
//IPAddress ip(192,168,0, 18);
//IPAddress gateway(192,168,0, 1);
//IPAddress subnet(255, 255, 255, 0);
// dont need to change the below vaules:
byte mac[] = { 0x1E, 0xDA, 0xBE, 0x5F, 0xF1, 0x3D};
IPAddress server(184,106,5,33); // marketplace.envato.com IP
//IPAddress server(192,168,0,12); //debug laptop
/* C QUESTION: should this client be defined here or just before where it is used in the loop()? Differences? */
EthernetClient client;
char api_url[] = "http://marketplace.envato.com/api/edge/";


/* C QUESTION: should these variables be declaired here or in the loop()? what difference does it make? */

long previousMillis = 0;        // will store last time we processed
float amount_today = 0;  // how much we have earnt today.
/* C QUESTION: do I need to initialise this statement_json to be an empty string? */
char statement_json[200];
unsigned int statement_json_ptr = 0;

unsigned int token_count = 0; // counts our tokens in the statement_json parsing.
boolean record_statement = false; // tells our client loop when to start recoring characters.
boolean latest_statement = true; // just a flag to say we're at the first statement item or not.
boolean isfunky = false; // used to help parsing funky characters.
float earnt_today = 0; // counter to help us work out how much was earnt today.
unsigned int charsread = 0; // how many { characters we've read. for json parsing.
char cr, last_cr = NULL; // individual chars for parsing our json from marketplace api

char *kind,*amount,*description; //,*occured_at // pointers to various bits in statement_json above
float amountf = 0; // the float conversion of "amount" so we can do arithmetic on it.

char todays_date[11]; // stores todays date. we use this to work out what sales were made today.
char this_statement_date[11]; // a string copy of "occured_at" so we can comare it to todays_date 
char last_statement_item_timestamp[19]; // when our last sale was made. so we can work out a second time round.
char this_statement_item_timestamp[19]; // a string copy of "occured_at" so we can comare it to todays_date 


    
void setup() {
  // start serial port:
  if(enableSerial)Serial.begin(9600);
  if(enableSerial)Serial.println("Starting...");
  pinMode(BUZZPIN, OUTPUT);
  pinMode(MOTORPIN, OUTPUT);
  
  // start the Ethernet connection:
  if (Ethernet.begin(mac) == 0) {
    if(enableSerial)Serial.println("Failed to configure Ethernet using DHCP");
    // no point in carrying on, so do nothing forevermore:
    while(true);
  }
  //Ethernet.begin(mac, ip, gateway, subnet); // or we can set manual IP. generates a smaller sketch.
  // give the Ethernet shield a second to initialize:
  delay(500);
  
  // start DMD stuff
  dmd.clearScreen( true );
  dmd.selectFont(Arial_14);
  Timer1.initialize( 2500 );           //period in microseconds to call ScanDMD. Anything longer than 5000 (5ms) and you can see flicker.
  Timer1.attachInterrupt( ScanDMD );  
  
  previousMillis = check_interval*1000; // so we can start straight away.
  
  set_current_balance(amount_today);
}

/*
 * Sets the current daily balance.
 * If the balance is higher than past balance, it will buzz as it counts up to new balance.
 */
void set_current_balance(int amount){
  if(enableSerial)Serial.println(amount);
  int c = 0;
  for(int i = amount_today; i < amount; i += INC_AMOUNT){
    //if(enableSerial)Serial.println(i);
    if(c++%5 == 0){
      digitalWrite(BUZZPIN,HIGH);
      delay(40);
      digitalWrite(BUZZPIN,LOW);
    }
    draw_dmd_dollar(i);
     delay(50);
  }
  amount_today = amount;
  draw_dmd_dollar(amount_today);
}

/*
 * We can spin a little motor when we make a sale etc..
 */
void spin_motor(){
    digitalWrite(MOTORPIN,HIGH);
    delay(40);
    digitalWrite(MOTORPIN,LOW); 
}

void loop() {
  unsigned long currentMillis = millis();
  if((currentMillis - previousMillis) < (check_interval*1000)) {
    if(enableSerial)Serial.println("skip... ");
    delay(1000);
    return;
  }
  previousMillis = currentMillis;   
    
  if(enableSerial)Serial.println("looping...");
  
  spin_motor(); // spin for debugging every time we loop. remove this later.
      
  earnt_today = 0;
  token_count = 0;
  record_statement = false;
  latest_statement = true;
  isfunky = false;
  
  
  //if(do_dmd_detach_during_network)Timer1.detachInterrupt( );  
  if (client.connect(server, 80)) {
    
    //if(enableSerial)Serial.print("Connected to API! Requesting: ");
    if(enableSerial)Serial.println(api_url);
    client.print("GET ");
    client.print(api_url);
    client.print(envato_username);
    client.print("/");
    client.print(envato_api_key);
    client.print("/statement.json");
    client.println(" HTTP/1.0");
    client.println("Host: marketplace.envato.com");
    client.println("User-Agent: arduino-dtbaker");
    client.println("Connection: close");
    client.println();
    //if(do_dmd_detach_during_network)Timer1.attachInterrupt( ScanDMD );  
    
    if(enableSerial)Serial.println("Sent.. waiting");

    // wait for envato to reply - is this needed? meh.
    delay(500);
    
    
    
    // we're looking for these strings:
    // {"kind":"sale","amount":"6.30","description":"Jump Eco HTML - Slider - Single product shop","occured_at":"Mon Jun 11 17:20:54 +1000 2012"},
    // {"kind":"referral_cut","amount":"18.00","description":"From referred user spireframe","occured_at":"Sat Jun 09 23:24:48 +1000 2012"},
    
    // the client.read() process converts them into a string like this:
    // kind:sale,amount:7.00,description:Paper Made - 3 Pages -  Photoshop Document,occured_at:Wed Jun 13 11|47|07 +1000 2012
    
    
    // the DMD display blinks badly when the network is receiving data.
    // so we disable the dmd display while we receive our data and then we do all the displaying after our network is finished and calculations are done.
    //dmd.clearScreen( true );
    draw_dmd_dollar(amount_today);
    
    charsread = 0;
    
    
    char latest_kind[13], latest_description[80];
    boolean do_notification = false;

    while (client.connected()) {
      if (client.available()) {
        if(do_dmd_detach_during_network)Timer1.detachInterrupt( );  
        cr = client.read();
        if(do_dmd_detach_during_network)Timer1.attachInterrupt( ScanDMD );  
        if (cr == '{') { 
          charsread++;// hack to skip the first { from {statement:
          if(charsread>1){
            record_statement = true;
            statement_json_ptr = 0;
            statement_json[statement_json_ptr] = '\0'; // clear our statement string ready for new characters from the api.
            
          }
        }else if (record_statement) {
          if(cr == '}'){
            // we've finished recording the latest statement! woo.
            // now - we process this statement
            
            //if(enableSerial)Serial.println();
            if(enableSerial)Serial.println("line:");
            if(enableSerial)Serial.println(statement_json);

            token_count = 0;
            
            boolean recordnext=false;
            int i;
            for(i = 0; i < sizeof(statement_json);i++){
              if(recordnext){
                recordnext=false;
                token_count++;
                switch(token_count){
                  case 1: // kind 
                    // only keep the kind for the latest item. for displaying. dont need it for the others.
                    //if(latest_statement)
                    kind=&statement_json[i];
                    break;
                  case 2: // amount
                    amount=&statement_json[i];
                    amountf = atof(amount);
                    break;
                  case 3: // description
                    // only keep the description for the latest item. for displaying on dmd
                    //if(latest_statement)
                    description=&statement_json[i];
                    break;
                  case 4: // occured at
                    statement_json[ (i+20 >= sizeof(statement_json)) ? sizeof(statement_json)-1 : i+20] = '\0';
                    strncpy(this_statement_item_timestamp, &statement_json[i], sizeof(this_statement_item_timestamp)-1);
                    //occured_at=&statement_json[i];
                    strncpy(this_statement_date, &statement_json[i], sizeof(this_statement_date)-1);
                    break;
                }
              }
              // we want to find any : characters
              // replace any , characters with \0
              if(statement_json[i] == ',')statement_json[i]='\0';
              if(statement_json[i] == ':')recordnext=true;
              // change them back
              if(statement_json[i] == '|')statement_json[i] = ':';
            }
                    
            //if(enableSerial)Serial.print("RESULTS Token count: ");
            //if(enableSerial)Serial.println(token_count);
            if(enableSerial)Serial.println(kind);
            if(enableSerial)Serial.println(amount);
            //if(enableSerial)Serial.println(amountf);
            if(enableSerial)Serial.println(description);
            if(enableSerial)Serial.println(this_statement_item_timestamp);
            if(enableSerial)Serial.println(this_statement_date);
            if(enableSerial)Serial.println(todays_date);
            
            
            if(token_count >= 4){
              if(latest_statement){
                latest_statement = false;
                // we are at the latest item in the statement.
                // set todays date based on this item. we use this to work out how much we've earnt today
                strncpy(todays_date, this_statement_date, sizeof(todays_date)-1); 
                
                
                
                
                // we have to check if this item is the same as the last one we found in the last loop.
                // we do this via comparing "this_timestamp" to "todays_date".
                if(strcmp(this_statement_item_timestamp,last_statement_item_timestamp)){
                  if(enableSerial)Serial.print("Last statement: ");
                  if(enableSerial)Serial.print(this_statement_item_timestamp);
                  if(enableSerial)Serial.print(" does not match last recoring: ");
                  if(enableSerial)Serial.print(last_statement_item_timestamp);
                  if(enableSerial)Serial.println(" so we're UPDATING OUR DIASPLAY!");
                  strncpy(last_statement_item_timestamp,this_statement_item_timestamp,sizeof(last_statement_item_timestamp)-1);
                  
                  do_notification = true;
                  
                  strncpy(latest_kind,kind,sizeof(latest_kind)-1);
                  strncpy(latest_description,description,sizeof(latest_description)-1);
                  //strcpy(latest_amount,amount);
                  //strncpy(latest_occured_at,occured_at,sizeof(latest_occured_at)-1);
                  
                  //if(do_dmd_detach_during_network)Timer1.attachInterrupt( ScanDMD ); 
                  //display_notification(kind, amount, description, this_statement_item_timestamp);
                  //if(do_dmd_detach_during_network)Timer1.detachInterrupt( );  
                  
                  
                }else{
                  if(enableSerial)Serial.print("Last statement: ");
                  if(enableSerial)Serial.print(this_statement_item_timestamp);
                  if(enableSerial)Serial.print(" matches last recoring: ");
                  if(enableSerial)Serial.print(last_statement_item_timestamp);
                  if(enableSerial)Serial.println(" so we're not doing anything");
                }
                
              }
                
              if(strcmp(this_statement_date,todays_date) == 0){
                if(enableSerial)Serial.print("TODAY");
                earnt_today = earnt_today + amountf;
                if(enableSerial)Serial.println(earnt_today);
              }else{
                // finished with todays totals.
                //set_current_balance(earnt_today);
                break; // stop processing rest of 
              }
              
            }else{
              if(enableSerial)Serial.println("ERROR ERROR! Cound not get valid API result!");
            }
            
            record_statement=false;
          }
          if(cr == ':' && last_cr != '"')cr = '|';// replace : with | so our json string tokenising works better
          // hack to handle \u0026 etc... - yep I don't like it. todo: find better way to handle these.
          if(isfunky){
            if(
              (last_cr == '\\' && cr == 'u') ||
              (last_cr == 'u' && (int)cr >= 48 && (int)cr <= 57) ||
              ((int)last_cr >= 48 && (int)last_cr <= 57 && (int)cr >= 48 && (int)cr <= 57)
            ){
              last_cr = cr;
              continue; 
            }
            else{
              last_cr = NULL;
             isfunky = false;
            }
          }else if(cr == '\\'){
            isfunky = true;
            last_cr = cr;
            continue;
          }
          if(last_cr != NULL && last_cr != '"' && last_cr != '[' && last_cr != '}'){
            //statement_json.concat(last_cr);

            if(statement_json_ptr >= sizeof(statement_json)-1)break;
            statement_json[statement_json_ptr] = last_cr;
            statement_json_ptr++;
            statement_json[statement_json_ptr] = '\0';
            /*char temp[ 2 ];
            temp[ 0 ] = last_cr;
            temp[ 1 ] = '\0';
            strcat (statement_json,temp);*/
          }
          last_cr = cr;
        }
      }
    
    } 
    //if(do_dmd_detach_during_network)Timer1.attachInterrupt( ScanDMD );  
  //
  if(do_notification){
    //display_notification(latest_kind, amount, latest_description, this_statement_item_timestamp);
    display_notification(latest_kind, latest_description);
  }
  
    set_current_balance(earnt_today);
    
    if(enableSerial)Serial.println("disconnecting.");
    client.stop();

  } 
  else {
    // if you couldn't make a connection:
    if(enableSerial)Serial.println("connection failed");
    if(enableSerial)Serial.println("disconnecting.");
    client.stop();
  }
  
              if(enableSerial)Serial.println(" ---- LOOP FINISHED ----- ");
}

void draw_dmd_dollar(int dollar){
  char a[6];
  sprintf(a,"$%d", dollar);
  draw_dmd_string(a);
}
void draw_dmd_string(const char *text){
  if(enableSerial)Serial.print("Drawing text: ");
  if(enableSerial)Serial.println(text);
  dmd.clearScreen( true );
  dmd.drawString(  1,  DMD_TOP_CHAR, text, strlen(text), GRAPHICS_NORMAL );
}
void draw_dmd_marquee(const char *text){
  if(enableSerial)Serial.print("Drawing marquee: ");
  if(enableSerial)Serial.println(text);
   dmd.clearScreen( true );
   
   dmd.drawMarquee(text,strlen(text),(32*da)-1,0);
   int start=millis();
   int timer=start;
   boolean ret=false;
   while(!ret){
     if ((timer+20) < millis()) { // change 40 to lower value to scroll faster.
       ret=dmd.stepMarquee(-1,0);
       timer=millis();
     }
   }
}


//void display_notification(char *kind, char *amount, char *description, char *occured_at){
void display_notification(char *kind, char *description){
    // we have all 4 items required for a valid API result!
    // time to make some noise.
    digitalWrite(BUZZPIN,HIGH);
    delay(500);
    digitalWrite(BUZZPIN,LOW);
    if(enableSerial)Serial.println("DISPLAY!");
    // check if this is a new occured_at.
    // if it is, display on on the screen!
    if(strcmp("sale",kind) == 0){
      // print out some text
       draw_dmd_marquee("You made a Sale!");
       // print out the dollar amount:
       /*char a[10] = "$";
       strcat(a,amount);
       draw_dmd_string(a);
       delay(1500);*/
       draw_dmd_marquee(description);
    }else if(strcmp("referral_cut",kind) == 0){
      // print out some text
       draw_dmd_marquee("Referral cut! Woo!");
       // print out the dollar amount:
       /*char a[10] = "$";
       strcat(a,amount);
       draw_dmd_string(a);
       delay(1500);*/
       draw_dmd_marquee(description);
    }else{
      // handle other cases (funds held? withdrawal? etc..)
      draw_dmd_marquee(kind);
      draw_dmd_marquee(description);
    }
}



